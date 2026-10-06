// =============================================================================
// sim/campaign.cpp — slice B1c: campaign CLI runner + threaded worker pool
//
// Single concern: **batch tournament execution + checkpointing + JSONL output**.
// No zoo bots, no analysis (later slices). Consumes the engine API as-is
// (run_tournament / AgentFactory / RngKind / TournamentMode).
//
// What this TU provides:
//   * `Registry` — a process-global name -> AgentFactory table with a
//     `REGISTER_AGENT(name)` link-time registration macro. Later slices add
//     zoo / candidate bots in their OWN translation units by defining the agent
//     class, a `make_agent_<Name>()` factory, and invoking
//     `REGISTER_AGENT(<Name>);` — no edits to this file, just linking.
//   * Two builtin agents, `AlwaysPeace` (always 0) and `AlwaysWar` (always 1),
//     registered unconditionally so the runner has a usable baseline pool for
//     this slice (see README "Gotchas" note on why they are not gated behind
//     CAMPAIGN_TEST).
//   * `campaign_run(argc, argv)` — the CLI:
//         --pool manifest.txt   lines: "name [count]" (count default 1)
//         --seed S              base seed; run i uses seed S + i
//         --mode distinct|self  (default distinct)
//         --runs K              number of runs (default 1)
//         --threads T           worker threads (default 16)
//         --out results.jsonl   output file (default results.jsonl)
//         --rng fast|fidelity   engine RNG kind (default fast)
//         --resume              skip run indices already present in --out
//     Total agents = sum of counts, capped at 200.
//   * Checkpoint/resume: with --resume the existing --out is parsed and any
//     run index already present (via "run":N) is skipped; because run i uses
//     the deterministic seed S+i, resume reproduces an uninterrupted run
//     exactly.
//
// JSONL line (one per run, appended under a lock, full double precision):
//   {"run":i,"seed":S+i,"mode":"...","pool":["Name",...],"scores":[...]}
//
// CRN (Common Random Numbers): paired A/B comparisons reuse the identical
// --seed and the same pool manifest modulo the single swapped agent, so the
// only thing that differs between the two runs is the agent under test
// (see README).
//
// Build: see sim/build.sh (targets `campaign` and `campaign_test`).
// =============================================================================

#include "engine.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace mas_sim {

// =============================================================================
// Global registry
// =============================================================================

// Process-global table of named agent factories. Registration is idempotent
// (last write wins for a given name) and safe to call from multiple TUs at
// static-init time, because each TU registers DISTINCT names in the normal
// zoo/candidate-bot case.
class Registry {
 public:
  using Factory = AgentFactory;

  static Registry& instance() {
    static Registry r;
    return r;
  }

  static bool register_factory(const std::string& name, Factory f) {
    instance().map_[name] = std::move(f);
    return true;
  }

  bool has(const std::string& name) const { return map_.count(name) != 0; }

  Factory get(const std::string& name) const {
    auto it = map_.find(name);
    if (it == map_.end()) {
      throw std::invalid_argument("unknown agent '" + name +
                                  "' (not registered in this build)");
    }
    return it->second;
  }

  size_t size() const { return map_.size(); }

  std::vector<std::string> names() const {
    std::vector<std::string> v;
    v.reserve(map_.size());
    for (const auto& kv : map_) v.push_back(kv.first);
    return v;
  }

 private:
  std::map<std::string, Factory> map_;
  Registry() = default;
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;
};

// Link-time registration macro. Usage in any TU:
//
//   class MyBot : public AgentBase { ... };
//   AgentFactory make_agent_MyBot() { return []{ return std::make_unique<MyBot>(); }; }
//   REGISTER_AGENT(MyBot);
//
// Expands to a static object whose constructor registers the factory with
// name "MyBot". Later slices add bots this way without touching this file.
#define REGISTER_AGENT(name)                                              \
  static const int mas_reg_agent_##name = []() {                         \
    ::mas_sim::Registry::register_factory(#name,                        \
                                          ::mas_sim::make_agent_##name()); \
    return 0;                                                            \
  }()

// =============================================================================
// Builtin baseline agents
//
// NOTE: registered UNCONDITIONALLY (not behind CAMPAIGN_TEST) so the
// standalone `campaign` runner has a usable pool for this slice; the
// CAMPAIGN_TEST ifdef below gates the self-test harness instead. See README.
// =============================================================================

class AlwaysPeaceAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return 0;
  }
};

class AlwaysWarAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return 1;
  }
};

AgentFactory make_agent_AlwaysPeace() {
  return [] { return std::make_unique<AlwaysPeaceAgent>(); };
}
AgentFactory make_agent_AlwaysWar() {
  return [] { return std::make_unique<AlwaysWarAgent>(); };
}

REGISTER_AGENT(AlwaysPeace);
REGISTER_AGENT(AlwaysWar);

// =============================================================================
// JSONL helpers
// =============================================================================

namespace {

// Minimal JSON string escaping (names are simple identifiers, but be safe).
std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

// Full double precision, deterministic formatting (17 significant digits is
// enough to round-trip an IEEE-754 double). Byte-identical across runs.
std::string fmt_double(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.17g", v);
  return buf;
}

// Build one JSONL line for a run.
std::string make_line(int run, int seed, const char* mode,
                      const std::vector<std::string>& pool,
                      const std::vector<double>& scores) {
  std::string line;
  line.reserve(128 + pool.size() * 16 + scores.size() * 24);
  line += "{\"run\":";
  line += std::to_string(run);
  line += ",\"seed\":";
  line += std::to_string(seed);
  line += ",\"mode\":\"";
  line += mode;
  line += "\",\"pool\":[";
  for (size_t i = 0; i < pool.size(); ++i) {
    if (i) line += ',';
    line += '"';
    line += json_escape(pool[i]);
    line += '"';
  }
  line += "],\"scores\":[";
  for (size_t i = 0; i < scores.size(); ++i) {
    if (i) line += ',';
    line += fmt_double(scores[i]);
  }
  line += "]}";
  return line;
}

// Parse an existing JSONL file, storing each line (raw, verbatim) at its
// run index in `lines` (sized `runs`) and recording completed indices in
// `done`. Lines whose run index is out of [0, runs) or malformed are ignored.
// Used by --resume so the final rewrite can keep already-completed lines
// verbatim and only (re)compute the missing ones — the output is then
// byte-identical to an uninterrupted run for ANY completion pattern
// (not just contiguous early stops).
void load_completed(const std::string& path, int runs,
                    std::vector<std::string>& lines, std::set<int>& done) {
  std::ifstream in(path);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    const size_t pos = line.find("\"run\":");
    if (pos == std::string::npos) continue;
    size_t p = pos + 6;
    int v = 0;
    bool any = false;
    while (p < line.size() && line[p] >= '0' && line[p] <= '9') {
      v = v * 10 + (line[p] - '0');
      ++p;
      any = true;
    }
    if (!any) continue;
    if (v < 0 || v >= runs) continue;
    if (lines[static_cast<std::size_t>(v)].empty()) {
      lines[static_cast<std::size_t>(v)] = line;  // keep verbatim
    }
    done.insert(v);
  }
}

}  // namespace

// =============================================================================
// Campaign runner
// =============================================================================

namespace {

struct CampaignConfig {
  std::string pool_path;
  int seed = 0;
  std::string mode = "distinct";
  int runs = 1;
  int threads = 16;
  std::string out = "results.jsonl";
  bool resume = false;
  RngKind rng = RngKind::kSfc64;
};

bool parse_int(const std::string& s, long& out) {
  if (s.empty()) return false;
  try {
    size_t pos = 0;
    long v = std::stol(s, &pos);
    if (pos != s.size()) return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_args(int argc, char** argv, CampaignConfig& cfg, std::string& err) {
  auto need_val = [&](int& i) -> const char* {
    if (i + 1 >= argc) {
      err = std::string("missing value for ") + argv[i];
      return nullptr;
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const char* v;
    if (a == "--pool") {
      if (!(v = need_val(i))) return false;
      cfg.pool_path = v;
    } else if (a == "--seed") {
      long l;
      if (!(v = need_val(i))) return false;
      if (!parse_int(v, l)) { err = "invalid --seed: " + std::string(v); return false; }
      cfg.seed = static_cast<int>(l);
    } else if (a == "--mode") {
      if (!(v = need_val(i))) return false;
      cfg.mode = v;
    } else if (a == "--runs") {
      long l;
      if (!(v = need_val(i))) return false;
      if (!parse_int(v, l) || l < 0) { err = "invalid --runs: " + std::string(v); return false; }
      cfg.runs = static_cast<int>(l);
    } else if (a == "--threads") {
      long l;
      if (!(v = need_val(i))) return false;
      if (!parse_int(v, l) || l < 1) { err = "invalid --threads: " + std::string(v); return false; }
      cfg.threads = static_cast<int>(l);
    } else if (a == "--out") {
      if (!(v = need_val(i))) return false;
      cfg.out = v;
    } else if (a == "--rng") {
      if (!(v = need_val(i))) return false;
      if (std::string(v) == "fidelity") cfg.rng = RngKind::kMsvcrt;
      else if (std::string(v) == "fast") cfg.rng = RngKind::kSfc64;
      else { err = "invalid --rng: " + std::string(v) + " (use fast|fidelity)"; return false; }
    } else if (a == "--resume") {
      cfg.resume = true;
    } else if (a == "--help" || a == "-h") {
      err = "__help__";
      return true;
    } else {
      err = "unknown argument: " + a;
      return false;
    }
  }
  return true;
}

void print_usage() {
  std::fprintf(stderr,
               "usage: campaign --pool manifest.txt --seed S [--mode "
               "distinct|self] [--runs K] [--threads T] [--out results.jsonl] "
               "[--rng fast|fidelity] [--resume]\n");
}

// Expand the manifest into an ordered agent-name list (repeating `name`
// `count` times per line). Enforces the 200-agent cap.
bool expand_manifest(const std::string& path, std::vector<std::string>& pool,
                     std::string& err) {
  std::ifstream in(path);
  if (!in) {
    err = "cannot open manifest: " + path;
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    // Strip comments and trim.
    auto hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::istringstream iss(line);
    std::string name;
    int count = 1;
    if (!(iss >> name)) continue;  // blank line
    long c = 1;
    if (iss >> c) {
      count = static_cast<int>(c);
    }
    if (name.empty() || count < 1) {
      err = "bad manifest line (count must be >= 1): " + line;
      return false;
    }
    if (count > 200) {
      err = "manifest line count " + std::to_string(count) +
            " exceeds per-line cap (total cap 200)";
      return false;
    }
    for (int i = 0; i < count; ++i) pool.push_back(name);
    if (static_cast<int>(pool.size()) > 200) {
      err = "total agents " + std::to_string(pool.size()) +
            " exceeds cap of 200";
      return false;
    }
  }
  if (pool.empty()) {
    err = "manifest produced no agents: " + path;
    return false;
  }
  return true;
}

}  // namespace

int campaign_run(int argc, char** argv) {
  CampaignConfig cfg;
  std::string err;
  if (!parse_args(argc, argv, cfg, err)) {
    if (err != "__help__") std::fprintf(stderr, "campaign: %s\n", err.c_str());
    print_usage();
    return err == "__help__" ? 0 : 2;
  }
  if (err == "__help__") {
    print_usage();
    return 0;
  }

  if (cfg.pool_path.empty()) {
    std::fprintf(stderr, "campaign: --pool is required\n");
    print_usage();
    return 2;
  }
  if (cfg.mode != "distinct" && cfg.mode != "self") {
    std::fprintf(stderr, "campaign: --mode must be 'distinct' or 'self'\n");
    return 2;
  }
  if (cfg.runs < 1) {
    std::fprintf(stderr, "campaign: --runs must be >= 1\n");
    return 2;
  }

  const TournamentMode mode =
      (cfg.mode == "distinct") ? TournamentMode::kDistinct
                               : TournamentMode::kSelfInclusive;

  // Expand manifest -> pool of names.
  std::vector<std::string> pool;
  if (!expand_manifest(cfg.pool_path, pool, err)) {
    std::fprintf(stderr, "campaign: %s\n", err.c_str());
    return 2;
  }
  const int n = static_cast<int>(pool.size());

  // Resolve names -> factories (throws on unknown; caught here).
  std::vector<AgentFactory> factories;
  factories.reserve(pool.size());
  try {
    for (const auto& name : pool) factories.push_back(Registry::instance().get(name));
  } catch (const std::exception& e) {
    std::fprintf(stderr, "campaign: %s\n", e.what());
    return 2;
  }

  // One pre-allocated slot per run, indexed by run. Under --resume the
  // already-completed lines are loaded verbatim into their slots up front;
  // workers then fill in only the missing run indices. After all workers
  // join, the main thread rewrites the WHOLE file in run-index order
  // (truncating first), so the result is byte-identical to an uninterrupted
  // run for ANY completion pattern — not just contiguous early stops.
  // Per-slot writes are race-free: each run index is claimed by exactly one
  // worker (atomic counter), and resume pre-loading happens before any worker
  // thread starts.
  std::set<int> done;
  std::vector<std::string> lines(static_cast<std::size_t>(cfg.runs));
  if (cfg.resume) load_completed(cfg.out, cfg.runs, lines, done);
  if (cfg.resume) {
    std::fprintf(stderr,
                 "campaign: --resume, %zu run(s) already present, skipping\n",
                 done.size());
  }

  // Always truncate: we are about to write the complete ordered file.
  std::ofstream out(cfg.out, std::ios::out | std::ios::trunc);
  if (!out) {
    std::fprintf(stderr, "campaign: cannot open output file: %s\n",
                 cfg.out.c_str());
    return 2;
  }

  std::mutex out_mutex;
  std::atomic<int> next_run{0};
  std::atomic<long> completed{0};
  const auto t0 = std::chrono::steady_clock::now();

  auto worker = [&]() {
    for (;;) {
      const int run = next_run.fetch_add(1);
      if (run >= cfg.runs) return;
      if (done.count(run)) {
        // Already checkpointed: nothing to do (still counts toward progress).
        completed.fetch_add(1);
        continue;
      }
      const int seed = cfg.seed + run;
      // Deterministic: engine reseeds its thread-local RNG from `seed`.
      const std::vector<double> scores =
          run_tournament(factories, n, seed, mode, cfg.rng);

      lines[static_cast<std::size_t>(run)] =
          make_line(run, seed, cfg.mode.c_str(), pool, scores);
      long c = completed.fetch_add(1) + 1;
      if (c % 1000 == 0) {
        const double el =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0)
                .count();
        std::lock_guard<std::mutex> lk(out_mutex);
        std::fprintf(stderr,
                     "progress: %ld runs, %.3fs, %.1f runs/sec\n", c, el,
                     (el > 0.0) ? (static_cast<double>(c) / el) : 0.0);
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(cfg.threads));
  for (int i = 0; i < cfg.threads; ++i) threads.emplace_back(worker);
  for (auto& t : threads) t.join();

  // Ordered flush: write every run index 0..runs-1 in order. Every slot is
  // guaranteed non-empty here (pre-loaded on resume, or computed by a worker).
  // The file was truncated at open, so this is the complete ordered output.
  for (int run = 0; run < cfg.runs; ++run) {
    out << lines[static_cast<std::size_t>(run)] << '\n';
  }
  out.flush();

  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
          .count();
  const long total = completed.load();
  const double rps = (elapsed > 0.0) ? (static_cast<double>(total) / elapsed) : 0.0;
  std::fprintf(stderr,
               "campaign: done %ld run(s) in %.3fs (%.1f runs/sec, %d "
               "threads, mode=%s, rng=%s, n=%d)\n",
               total, elapsed, rps, cfg.threads, cfg.mode.c_str(),
               (cfg.rng == RngKind::kSfc64) ? "fast" : "fidelity", n);
  return 0;
}

// =============================================================================
// Self-test (builtin bots), gated by CAMPAIGN_TEST
// =============================================================================

#ifdef CAMPAIGN_TEST

int campaign_self_test() {
  std::printf("=== campaign_self_test ===\n");
  int failures = 0;
  auto report = [&](bool ok, const char* name) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
  };

  const auto& reg = Registry::instance();
  report(reg.has("AlwaysPeace") && reg.has("AlwaysWar"),
         "registry has builtin AlwaysPeace and AlwaysWar");

  // Determinism: same seed twice -> identical, finite score arrays.
  const std::vector<std::string> pool = {"AlwaysPeace", "AlwaysWar",
                                         "AlwaysPeace"};
  std::vector<AgentFactory> f;
  for (auto& name : pool) f.push_back(reg.get(name));
  const int n = static_cast<int>(pool.size());
  const auto s1 = run_tournament(f, n, 12345, TournamentMode::kDistinct,
                                 RngKind::kSfc64);
  const auto s2 = run_tournament(f, n, 12345, TournamentMode::kDistinct,
                                 RngKind::kSfc64);
  bool det = s1.size() == static_cast<size_t>(n) && s1 == s2;
  for (double v : s1)
    if (!std::isfinite(v)) det = false;
  report(det, "same seed => identical, finite score arrays");

  // Hand-computed exact scores for pool {Peace(0), War(1), Peace(0)},
  // distinct mode (n=3 => the single triple (0,1,2)). Every round the
  // actions are fixed (a=0,b=1,c=0), so per round:
  //   p0: payoff[0][1][0]=1, p1: payoff[1][0][0]=6, p2: payoff[0][0][1]=1
  // Over R rounds: match_results=[R,6R,R], score = match_results/R =
  // [1, 6, 1] — independent of the drawn horizon, so the exact value is
  // a robust, seed-independent assertion on the runner's accumulation.
  const double expect[3] = {1.0, 6.0, 1.0};
  bool exact = s1.size() == static_cast<size_t>(n);
  for (int i = 0; i < n && exact; ++i) {
    if (std::fabs(s1[static_cast<size_t>(i)] - expect[i]) > 1e-9) exact = false;
  }
  report(exact, "hand-computed {Peace,War,Peace} distinct scores == [1,6,1]");

  if (failures == 0) {
    std::printf("ALL PASS (%d checks)\n", 3);
    return 0;
  }
  std::printf("FAIL: %d check(s) failed\n", failures);
  return 1;
}

#endif  // CAMPAIGN_TEST

}  // namespace mas_sim

#ifndef CAMPAIGN_TEST
int main(int argc, char** argv) { return mas_sim::campaign_run(argc, argv); }
#endif  // !CAMPAIGN_TEST
