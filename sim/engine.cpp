// =============================================================================
// sim/engine.cpp — implementation of sim/engine.hpp (slice B1a)
// =============================================================================

#include "engine.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace mas_sim {

// -----------------------------------------------------------------------------
// RNGs
// -----------------------------------------------------------------------------

namespace {

// splitmix64 mixer (Blackman & Vela), used to seed sfc64.
inline uint64_t splitmix64(uint64_t* state) {
  uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

}  // namespace

void Rng::reseed(int seed) {
  uint64_t sm = static_cast<uint64_t>(static_cast<uint32_t>(seed));
  uint64_t s = splitmix64(&sm);
  mask_ = splitmix64(&sm) | 1ull;  // odd => full sfc64 period
  // sfc64 uses a counter (the "result" stream) that must differ from the
  // additive constant s; if it doesn't the generator is stuck at a fixed
  // point (sfc64(0) == s for every call). Perturb until distinct.
  s_ = s;
  if (s_ == mask_) {
    s_ += 0x9E3779B97F4A7C15ull;
    if (s_ == mask_) s_ += 0x9E3779B97F4A7C15ull;
  }
  result_ = 0;
}

uint64_t Rng::sfc64() {
  uint64_t t = result_ + mask_;
  result_ += mask_;
  t ^= t >> 30;
  t *= 0xBF58476D1CE4E5B9ull;
  t ^= t >> 27;
  t *= 0x94D049BB133111EBull;
  t ^= t >> 31;
  return t + s_;
}

MsvcrtRng& fidelity_rng() {
  thread_local MsvcrtRng r(0);
  return r;
}

Rng& fast_rng() {
  thread_local Rng r(0);
  return r;
}

// -----------------------------------------------------------------------------
// Payoff tensor — copied VERBATIM from ThreeWayPeaceWar.cpp:
//   {{4, 1}, {1, -1}},
//   {{6, 3}, {3, 0}}
// Player X reads payoff[X][next][prev] (B is X's next, C is X's prev).
// -----------------------------------------------------------------------------

namespace {

constexpr int kPayoff[2][2][2] = {
    {{4, 1}, {1, -1}},
    {{6, 3}, {3, 0}},
};

// Sample-faithful horizon: 90 + round(20 * (float)rand() / RAND_MAX),
// RAND_MAX = 0x7FFF, float division, int result.
inline int draw_rounds(RngKind kind) {
  const int r = (kind == RngKind::kMsvcrt) ? fidelity_rng().rand()
                                           : fast_rng().rand();
  return 90 + round(20.0f * (static_cast<float>(r) /
                             static_cast<float>(kMsvcrtRandMax)));
}

}  // namespace

// -----------------------------------------------------------------------------
// Match playback
// -----------------------------------------------------------------------------

MatchResult play_match(std::unique_ptr<AgentBase> a, std::unique_ptr<AgentBase> b,
                       std::unique_ptr<AgentBase> c, RngKind rng_kind,
                       int max_rounds) {
  // The horizon is drawn AFTER agent construction (the sample constructs
  // A, B, C first, so constructor-side RNG draws — e.g. the sample's
  // FreakyAgent — happen before the horizon draw).
  const int rounds = draw_rounds(rng_kind);

  // Sample-faithful types: int matchResults[3], accumulated in int.
  // (The float cast + division happens at scoring time, mirroring
  // `float matchResults[3]` / `totalScore[i] += matchResults[x] / rounds`
  // in the sample — where the float division promotes the int to float.)
  int match_results[3] = {0, 0, 0};

  // Flat preallocated history storage: reserved once up front, cleared
  // per match, index appends only — no per-round reallocation.
  std::vector<int> ha, hb, hc;
  ha.reserve(max_rounds);
  hb.reserve(max_rounds);
  hc.reserve(max_rounds);

  for (int l = 0; l < rounds; ++l) {
    // Agents see the histories as they were before this round (length l),
    // exactly what the sample's std::vector histories contain at call time.
    const int play_a = a->selectAction(l, ha, hb, hc);
    const int play_b = b->selectAction(l, hb, hc, ha);
    const int play_c = c->selectAction(l, hc, ha, hb);
    // Payoffs collected BEFORE histories are extended.
    match_results[0] += kPayoff[play_a][play_b][play_c];
    match_results[1] += kPayoff[play_b][play_c][play_a];
    match_results[2] += kPayoff[play_c][play_a][play_b];
    ha.push_back(play_a);
    hb.push_back(play_b);
    hc.push_back(play_c);
  }

  // a, b, c destroyed here — per-match fresh construct + delete, as in
  // the sample.
  return MatchResult{
      rounds,
      {static_cast<float>(match_results[0]),
       static_cast<float>(match_results[1]),
       static_cast<float>(match_results[2])}};
}

// -----------------------------------------------------------------------------
// Tournament
// -----------------------------------------------------------------------------

std::vector<double> run_tournament(const std::vector<AgentFactory>& factories,
                                   int num_agents, int seed,
                                   TournamentMode mode, RngKind rng_kind) {
  if (factories.size() != static_cast<std::size_t>(num_agents) || num_agents < 1) {
    throw std::invalid_argument(
        "run_tournament: factories.size() must equal num_agents >= 1");
  }

  // Re-seed the active RNG (thread-local) for this tournament. We do NOT
  // seed the C global rand(): on a non-MSVC host glibc's rand() is a
  // different algorithm (and RAND_MAX is 2^31-1, not 0x7FFF), so it would
  // never reproduce the MSVCRT stream. The single canonical stream for a
  // given mode is the thread-local handle below; sample-faithful stochastic
  // bots (later slice) route their "rand()" calls through fidelity_rand() /
  // fast_rand() to stay bit-exact.
  if (rng_kind == RngKind::kMsvcrt) {
    fidelity_rng().reset(seed);
  } else {
    fast_rng().reseed(seed);
  }

  // Sample-faithful types: float totalScore[numAgents].
  std::vector<float> total_score(static_cast<std::size_t>(num_agents), 0.0f);

  // Enumeration: kSelfInclusive is the sample's exact loop (i <= j <= k);
  // kDistinct is i < j < k.
  for (int i = 0; i < num_agents; ++i) {
    const int j_lo = (mode == TournamentMode::kSelfInclusive) ? i : i + 1;
    for (int j = j_lo; j < num_agents; ++j) {
      const int k_lo = (mode == TournamentMode::kSelfInclusive) ? j : j + 1;
      for (int k = k_lo; k < num_agents; ++k) {
        // Construct first (constructor-side RNG draws precede the
        // horizon draw, as in the sample).
        std::unique_ptr<AgentBase> a = factories[i]();
        std::unique_ptr<AgentBase> b = factories[j]();
        std::unique_ptr<AgentBase> c = factories[k]();

        MatchResult m =
            play_match(std::move(a), std::move(b), std::move(c), rng_kind);

        // Sample-faithful scoring, types reproduced:
        //   float totalScore[i] += matchResults[0] / rounds;  (float / int)
        total_score[static_cast<std::size_t>(i)] +=
            m.results[0] / static_cast<float>(m.rounds);
        total_score[static_cast<std::size_t>(j)] +=
            m.results[1] / static_cast<float>(m.rounds);
        total_score[static_cast<std::size_t>(k)] +=
            m.results[2] / static_cast<float>(m.rounds);
      }
    }
  }

  std::vector<double> out(total_score.size());
  for (std::size_t i = 0; i < total_score.size(); ++i) {
    out[i] = static_cast<double>(total_score[i]);
  }
  return out;
}

// =============================================================================
// Self-test
// =============================================================================

namespace {

int g_failures = 0;

void report(bool ok, const char* name) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) ++g_failures;
}

int64_t binom(int64_t n, int64_t r) {
  if (r < 0 || r > n) return 0;
  if (r > n - r) r = n - r;
  int64_t acc = 1;
  for (int64_t i = 1; i <= r; ++i) acc = acc * (n - r + i) / i;
  return acc;
}

// C(n+2, 3): number of self-inclusive triples i<=j<=k over n agents.
int64_t matches_self_inclusive(int n) { return binom(n + 2, 3); }
// C(n, 3): number of distinct triples i<j<k over n agents.
int64_t matches_distinct(int n) { return binom(n, 3); }

// The 8 rows of the payoff tensor, as a hand table.
struct PayoffRow {
  int a, b, c, v;
};
constexpr PayoffRow kHandTable[8] = {
    {0, 0, 0, 4},  {0, 0, 1, 1},  {0, 1, 0, 1},  {0, 1, 1, -1},
    {1, 0, 0, 6},  {1, 0, 1, 3},  {1, 1, 0, 3},  {1, 1, 1, 0},
};

// ---------------------------------------------------------------------------
// Deterministic agents for the scripted-match check.
//
//   A: always 0
//   B: l % 2          ->  0, 1, 0, 1, ...
//   C: (l + 1) % 2    ->  1, 0, 1, 0, ...
//
// Hand-computed per-round payoffs (looked up in the tensor by hand):
//   l even  (a,b,c)=(0,0,1): A=payoff[0][0][1]=1, B=payoff[0][1][0]=1,
//                            C=payoff[1][0][0]=6
//   l odd   (a,b,c)=(0,1,0): A=payoff[0][1][0]=1, B=payoff[1][0][0]=6,
//                            C=payoff[0][0][1]=1
//
// So for R rounds with E = (R+1)/2 even rounds and O = R/2 odd rounds:
//   totalA = R,  totalB = E + 6O,  totalC = 6E + O
//   match scores: A = R/R = 1,  B = (E + 6O)/R,  C = (6E + O)/R
// ---------------------------------------------------------------------------

class ScriptA : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return 0;
  }
};
class ScriptB : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return n % 2;
  }
};
class ScriptC : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return (n + 1) % 2;
  }
};

class AlwaysPeace : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return 0;
  }
};

// Alternating agent: action depends only on the round number (deterministic,
// no RNG) — used so that match scores depend on the drawn horizon.
class Alternating : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return n % 2;
  }
};

AgentFactory make_always_peace() {
  return [] { return std::make_unique<AlwaysPeace>(); };
}
AgentFactory make_alternating() {
  return [] { return std::make_unique<Alternating>(); };
}

// ---------------------------------------------------------------------------
// Reference replay for the match-count check: re-run the same LCG in the
// same enumeration order as run_tournament(kMsvcrt) with always-0 agents
// and accumulate in double. For all-0 agents each match scores
// 4*rounds, i.e. (4*rounds)/rounds = 4.0 per slot per match, so the
// per-agent total is 4.0 * (number of matches that include that agent).
// Returns the LCG state after the replay, so a match-count check can
// compare it against the engine's post-tournament RNG state: a wrong
// enumeration would consume a different number of horizon draws and land
// on a different LCG state.
// ---------------------------------------------------------------------------

struct RefTotals {
  std::vector<double> per_agent;
  unsigned int final_rng_state;
};

RefTotals reference_allzero(int n, TournamentMode mode, int seed) {
  MsvcrtRng rng(seed);
  std::vector<double> total(static_cast<std::size_t>(n), 0.0);
  for (int i = 0; i < n; ++i) {
    const int j_lo = (mode == TournamentMode::kSelfInclusive) ? i : i + 1;
    for (int j = j_lo; j < n; ++j) {
      const int k_lo = (mode == TournamentMode::kSelfInclusive) ? j : j + 1;
      for (int k = k_lo; k < n; ++k) {
        // Consume the horizon draw (keeps the LCG in step with the
        // engine). For all-0 agents every round scores
        // payoff[0][0][0] = 4, so match_results = 4*rounds and the
        // per-match contribution to each slot is
        // (4*rounds)/rounds = 4.0 — independent of the drawn horizon.
        (void)(90 + round(20.0f * (static_cast<float>(rng.rand()) /
                                   static_cast<float>(kMsvcrtRandMax))));
        total[static_cast<std::size_t>(i)] += 4.0;
        total[static_cast<std::size_t>(j)] += 4.0;
        total[static_cast<std::size_t>(k)] += 4.0;
      }
    }
  }
  return RefTotals{total, rng.state()};
}

// ---------------------------------------------------------------------------
// Independent reference simulator for the score-sum check:
// re-implements the match loop for deterministic agents (no RNG in
// constructors), re-runs the LCG for the horizon, and accumulates in
// double.
//
//   alt_index : agent that uses the Alternating strategy (a(l) = l%2)
//   others    : AlwaysPeace (a(l) = 0)
// ---------------------------------------------------------------------------

std::vector<double> reference_sim(int n, TournamentMode mode, int seed,
                                  int alt_index) {
  MsvcrtRng rng(seed);
  std::vector<double> total(static_cast<std::size_t>(n), 0.0);
  const auto action_of = [&](int agent, int l) {
    return (agent == alt_index) ? (l % 2) : 0;
  };

  for (int i = 0; i < n; ++i) {
    const int j_lo = (mode == TournamentMode::kSelfInclusive) ? i : i + 1;
    for (int j = j_lo; j < n; ++j) {
      const int k_lo = (mode == TournamentMode::kSelfInclusive) ? j : j + 1;
      for (int k = k_lo; k < n; ++k) {
        const int rounds =
            90 + round(20.0f * (static_cast<float>(rng.rand()) /
                                static_cast<float>(kMsvcrtRandMax)));
        int m[3] = {0, 0, 0};
        for (int l = 0; l < rounds; ++l) {
          const int a = action_of(i, l);
          const int b = action_of(j, l);
          const int c = action_of(k, l);
          m[0] += kPayoff[a][b][c];
          m[1] += kPayoff[b][c][a];
          m[2] += kPayoff[c][a][b];
        }
        total[static_cast<std::size_t>(i)] +=
            static_cast<double>(m[0]) / rounds;
        total[static_cast<std::size_t>(j)] +=
            static_cast<double>(m[1]) / rounds;
        total[static_cast<std::size_t>(k)] +=
            static_cast<double>(m[2]) / rounds;
      }
    }
  }
  return total;
}

// ---------------------------------------------------------------------------
// Check 1: payoff tensor vs the 8-row hand table.
// ---------------------------------------------------------------------------

void check_payoff_tensor() {
  bool ok = true;
  for (int row = 0; row < 8; ++row) {
    const auto& h = kHandTable[row];
    const int actual = kPayoff[h.a][h.b][h.c];
    if (actual != h.v) {
      std::printf("      mismatch at payoff[%d][%d][%d]: got %d, expected %d\n",
                  h.a, h.b, h.c, actual, h.v);
      ok = false;
    }
  }
  report(ok, "payoff tensor matches 8-row hand table");
}

// ---------------------------------------------------------------------------
// Check 2: horizon draws hit every value 90..110 over 10k draws, in both
// RNG modes.
// ---------------------------------------------------------------------------

void check_horizon_distribution() {
  constexpr int kDraws = 10000;

  // --- msvcrt LCG ---
  {
    int seen[111] = {0};
    MsvcrtRng rng(12345);
    bool in_range = true;
    for (int d = 0; d < kDraws; ++d) {
      const int r =
          90 + round(20.0f * (static_cast<float>(rng.rand()) /
                              static_cast<float>(kMsvcrtRandMax)));
      if (r < 90 || r > 110) {
        std::printf("      msvcrt draw %d out of range: %d\n", d, r);
        in_range = false;
      } else {
        ++seen[r];
      }
    }
    bool ok = in_range;
    for (int v = 90; v <= 110; ++v) {
      if (seen[v] == 0) {
        std::printf("      msvcrt: horizon value %d not seen in %d draws\n", v,
                    kDraws);
        ok = false;
      }
    }
    report(ok, "msvcrt horizon: all values 90..110 hit in 10k draws");
  }

  // --- sfc64 (fast) ---
  {
    int seen[111] = {0};
    Rng rng(12345);
    bool in_range = true;
    for (int d = 0; d < kDraws; ++d) {
      const int r =
          90 + round(20.0f * (static_cast<float>(rng.rand()) /
                              static_cast<float>(kMsvcrtRandMax)));
      if (r < 90 || r > 110) {
        std::printf("      sfc64 draw %d out of range: %d\n", d, r);
        in_range = false;
      } else {
        ++seen[r];
      }
    }
    bool ok = in_range;
    for (int v = 90; v <= 110; ++v) {
      if (seen[v] == 0) {
        std::printf("      sfc64: horizon value %d not seen in %d draws\n", v,
                    kDraws);
        ok = false;
      }
    }
    report(ok, "sfc64 horizon: all values 90..110 hit in 10k draws");
  }
}

// ---------------------------------------------------------------------------
// Check 3: one scripted 3-bot match vs hand-computed per-round payoffs
// (closed form derived by hand above; cross-checked against the tensor).
// ---------------------------------------------------------------------------

void check_scripted_match() {
  constexpr int kSeed = 77;

  // The engine draws the horizon from the thread-local fidelity RNG;
  // reset it the same way run_tournament does, then play one match.
  fidelity_rng().reset(kSeed);

  MatchResult m = play_match(std::make_unique<ScriptA>(),
                             std::make_unique<ScriptB>(),
                             std::make_unique<ScriptC>(),
                             RngKind::kMsvcrt);

  // The LCG with seed 77 draws 290 first, so the horizon must be
  // 90 + round(20 * 290/32767) = 90 + round(0.177) = 90.
  if (m.rounds != 90) {
    std::printf("      horizon mismatch: got %d rounds, expected 90 "
                "(seed 77, first LCG draw 290)\n", m.rounds);
  }

  // Independent per-round tensor cross-check (same lookup order the
  // engine uses): confirms the closed form below.
  bool per_round_ok = true;
  for (int l = 0; l < m.rounds; ++l) {
    const int a = 0, b = l % 2, c = (l + 1) % 2;
    const int expect_a = (l % 2 == 0) ? 1 : 1;
    const int expect_b = (l % 2 == 0) ? 1 : 6;
    const int expect_c = (l % 2 == 0) ? 6 : 1;
    if (kPayoff[a][b][c] != expect_a || kPayoff[b][c][a] != expect_b ||
        kPayoff[c][a][b] != expect_c) {
      std::printf(
          "      per-round tensor cross-check mismatch at round %d: "
          "a=%d b=%d c=%d\n",
          l, a, b, c);
      per_round_ok = false;
    }
  }

  // Hand closed form.
  const int R = m.rounds;
  const int E = (R + 1) / 2;  // even rounds
  const int O = R / 2;        // odd rounds
  const double expect[3] = {
      1.0,
      static_cast<double>(E + 6 * O) / R,
      static_cast<double>(6 * E + O) / R,
  };

  bool ok = per_round_ok && (m.rounds == 90);
  for (int x = 0; x < 3; ++x) {
    const double actual =
        static_cast<double>(m.results[x]) / static_cast<double>(R);
    if (std::fabs(actual - expect[x]) > 1e-5) {
      std::printf("      bot %d: got %.6f, expected %.6f (R=%d)\n", x,
                  actual, expect[x], R);
      ok = false;
    }
  }
  report(ok, "scripted 3-bot match matches hand-computed per-round payoffs");
}

// ---------------------------------------------------------------------------
// Check 4a: match counts — both enumeration modes consume exactly
// C(n+2,3) / C(n,3) LCG draws (one horizon draw per match), verified by
// comparing the engine's post-tournament LCG state with a reference
// replay. All-0 agents draw nothing in constructors, so this is exact.
// ---------------------------------------------------------------------------

void check_match_counts() {
  constexpr int kSeed = 2024;  // LCG seed with a well-mixed first draw

  struct Case {
    int n;
    TournamentMode mode;
    int64_t expected_matches;
  };
  const Case cases[] = {
      {1, TournamentMode::kSelfInclusive, 1},   // C(3,3) = 1
      {1, TournamentMode::kDistinct, 0},        // C(1,3) = 0
      {3, TournamentMode::kSelfInclusive, 10},  // C(5,3) = 10
      {3, TournamentMode::kDistinct, 1},       // C(3,3) = 1
      {4, TournamentMode::kSelfInclusive, 20},  // C(6,3) = 20
      {4, TournamentMode::kDistinct, 4},       // C(4,3) = 4
      {7, TournamentMode::kSelfInclusive, 84},  // C(9,3) = 84
      {7, TournamentMode::kDistinct, 35},       // C(7,3) = 35
  };
  constexpr size_t kNumCases = 8;

  // Sanity: the closed forms must agree with direct counting.
  for (size_t c = 0; c < kNumCases; ++c) {
    const int64_t by_formula =
        (cases[c].mode == TournamentMode::kSelfInclusive)
            ? matches_self_inclusive(cases[c].n)
            : matches_distinct(cases[c].n);
    if (by_formula != cases[c].expected_matches) {
      std::printf("      closed form C(%d,3)=%lld != expected %lld\n",
                  (cases[c].mode == TournamentMode::kSelfInclusive)
                      ? cases[c].n + 2
                      : cases[c].n,
                  static_cast<long long>(by_formula),
                  static_cast<long long>(cases[c].expected_matches));
    }
  }

  for (size_t c = 0; c < kNumCases; ++c) {
    const int n = cases[c].n;
    std::vector<AgentFactory> f(static_cast<std::size_t>(n),
                                make_always_peace());

    const std::vector<double> scores =
        run_tournament(f, n, kSeed, cases[c].mode, RngKind::kMsvcrt);

    bool ok = scores.size() == static_cast<std::size_t>(n);

    // Match-count check via RNG state.
    const RefTotals ref = reference_allzero(n, cases[c].mode, kSeed);
    if (fidelity_rng().state() != ref.final_rng_state) {
      std::printf(
          "      n=%d mode=%s: match count mismatch — engine LCG state "
          "0x%08X != reference 0x%08X (expected %lld matches)\n",
          n,
          cases[c].mode == TournamentMode::kSelfInclusive ? "self-inclusive"
                                                          : "distinct",
          fidelity_rng().state(), ref.final_rng_state,
          static_cast<long long>(cases[c].expected_matches));
      ok = false;
    }

    // Score check: each all-0 match contributes exactly 4.0 per player.
    if (ok) {
      for (int i = 0; i < n; ++i) {
        if (std::fabs(scores[static_cast<std::size_t>(i)] -
                      ref.per_agent[static_cast<std::size_t>(i)]) > 1e-4) {
          std::printf(
              "      n=%d mode=%s: agent %d got %.6f, expected %.6f\n", n,
              cases[c].mode == TournamentMode::kSelfInclusive
                  ? "self-inclusive"
                  : "distinct",
              i, scores[static_cast<std::size_t>(i)],
              ref.per_agent[static_cast<std::size_t>(i)]);
          ok = false;
        }
      }
    }

    const char* mode_name =
        (cases[c].mode == TournamentMode::kSelfInclusive)
            ? "self-inclusive (i<=j<=k)"
            : "distinct (i<j<k)";
    report(ok,
           (std::string("match count: n=") + std::to_string(n) + " " +
            mode_name + " — C(" +
            std::to_string((cases[c].mode == TournamentMode::kSelfInclusive)
                               ? n + 2
                               : n) +
            ",3)=" + std::to_string(cases[c].expected_matches) +
            " horizon draws").c_str());
  }
}

// ---------------------------------------------------------------------------
// Check 4b: score sums — agent 0 uses the deterministic Alternating
// strategy, the rest AlwaysPeace, so match scores depend on the drawn
// horizon. Engine output is compared against an independent double-
// precision reference simulator of the same semantics.
// ---------------------------------------------------------------------------

void check_score_sums() {
  constexpr int kSeed = 31337;  // LCG seed with a well-mixed stream
  constexpr double kTol = 1e-3;  // float accumulation over <= 84 matches

  struct Case {
    int n;
    TournamentMode mode;
  };
  const Case cases[] = {
      {3, TournamentMode::kSelfInclusive},
      {3, TournamentMode::kDistinct},
      {5, TournamentMode::kSelfInclusive},
      {5, TournamentMode::kDistinct},
  };
  constexpr size_t kNumCases = 4;

  for (size_t c = 0; c < kNumCases; ++c) {
    const int n = cases[c].n;
    std::vector<AgentFactory> f(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
      f[static_cast<std::size_t>(i)] =
          (i == 0) ? make_alternating() : make_always_peace();
    }

    const std::vector<double> scores =
        run_tournament(f, n, kSeed, cases[c].mode, RngKind::kMsvcrt);

    const std::vector<double> ref =
        reference_sim(n, cases[c].mode, kSeed, /*alt_index=*/0);

    bool ok = scores.size() == static_cast<std::size_t>(n);
    if (ok) {
      for (int i = 0; i < n; ++i) {
        if (std::fabs(scores[static_cast<std::size_t>(i)] -
                      ref[static_cast<std::size_t>(i)]) > kTol) {
          std::printf(
              "      n=%d mode=%s: agent %d got %.6f, expected %.6f\n", n,
              cases[c].mode == TournamentMode::kSelfInclusive
                  ? "self-inclusive"
                  : "distinct",
              i, scores[static_cast<std::size_t>(i)],
              ref[static_cast<std::size_t>(i)]);
          ok = false;
        }
      }
    }

    const char* mode_name =
        (cases[c].mode == TournamentMode::kSelfInclusive)
            ? "self-inclusive (i<=j<=k)"
            : "distinct (i<j<k)";
    report(ok, (std::string("score sums: n=") + std::to_string(n) + " " +
               mode_name).c_str());
  }
}

// ---------------------------------------------------------------------------
// Check 5: throughput — fast mode, 100 always-peace agents, both
// enumeration modes; target >= 5 full tournaments per core-second.
// (Excluded from sanitizer/debug builds — see engine_self_test.)
// ---------------------------------------------------------------------------

#if !defined(ENGINE_SANITIZED)

void check_throughput() {
  constexpr int kNumAgents = 100;
  constexpr double kDurationSec = 0.5;
  constexpr double kTarget = 5.0;

  std::vector<AgentFactory> f(kNumAgents, make_always_peace());

  struct Result {
    const char* name;
    int64_t matches;
    double per_sec;
  };
  Result res[2];

  for (int t = 0; t < 2; ++t) {
    const TournamentMode mode = (t == 0) ? TournamentMode::kDistinct
                                         : TournamentMode::kSelfInclusive;
    // Warm-up (page faults, icache).
    run_tournament(f, kNumAgents, 1, mode, RngKind::kSfc64);

    auto t0 = std::chrono::steady_clock::now();
    int count = 0;
    for (int s = 0; s < 1000000; ++s) {
      run_tournament(f, kNumAgents, 2 + s, mode, RngKind::kSfc64);
      ++count;
      const auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration<double>(now - t0).count() >= kDurationSec) {
        break;
      }
    }
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
    res[t] = {
        (t == 0) ? "distinct   " : "self-incls",
        (t == 0) ? matches_distinct(kNumAgents)
                 : matches_self_inclusive(kNumAgents),
        static_cast<double>(count) / elapsed,
    };
  }

  bool ok = true;
  for (const auto& r : res) {
    std::printf("      %-11s: %10.1f tournaments/sec/core "
                "(%lld matches each)\n",
                r.name, r.per_sec, static_cast<long long>(r.matches));
    if (r.per_sec < kTarget) ok = false;
  }
  report(ok, "fast-mode throughput >= 5 tournaments/sec/core (100 agents)");
}

#endif  // !ENGINE_SANITIZED

}  // namespace

int engine_self_test() {
  std::printf("=== engine_self_test ===\n");

  check_payoff_tensor();
  check_horizon_distribution();
  check_scripted_match();
  check_match_counts();
  check_score_sums();
  // Throughput is only meaningful on an optimized build; under
  // ASan/UBSan (or a debug -O0 build, -DENGINE_SANITIZED) the runtime
  // is ~20x slower and the target is unreachable by construction.
#if !defined(ENGINE_SANITIZED)
  check_throughput();
  const int kGroups = 6;
#else
  std::printf("  [SKIP] fast-mode throughput (sanitizer/debug build)\n");
  const int kGroups = 5;
#endif

  if (g_failures == 0) {
    std::printf("ALL PASS (%d check groups)\n", kGroups);
    return 0;
  }
  std::printf("FAIL: %d check group(s) failed\n", g_failures);
  return 1;
}

}  // namespace mas_sim
