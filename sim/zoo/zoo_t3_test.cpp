// =============================================================================
// sim/zoo/zoo_t3_test.cpp — slice B3b: behavioral tests for the Tier-3 zoo
//
// Mirrors zoo_t2_test.cpp conventions exactly:
//   (i)  scripted-history unit tests: build a bot via the Registry and call
//        selectAction() with crafted myHistory/opp1/opp2 vectors, asserting
//        the exact expected action. Stateful bots (AdaptModel5 ring, MetaV1
//        commit) are driven through consistent histories on one instance;
//        stochastic decisions are made deterministic by reseeding the
//        engine's fast_rng() and predicting the draw with a throwaway Rng
//        of the same seed.
//   (ii) smoke tournament: the 5 Tier-3 bots + 6 Tier-0 anchors, 5 runs,
//        fast mode, distinct enumeration; assert all scores finite, no
//        exceptions, same-seed determinism.
//   (iii) sanity checks: ColludeSqueeze vs {Nice,Nice}; ZDExtort3P vs
//        {Nasty,Nasty}; CounterProber vs {Alternator, NiceAgent}.
//
// Build: sim/build.sh `zoo` (links engine.cpp + zoo_t01 + zoo_t2 + zoo_t3).
// =============================================================================

#include "../engine.hpp"
#include "zoo_common.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using mas_sim::AgentBase;
using mas_sim::Rng;
using mas_sim::Registry;
using mas_sim::run_tournament;
using mas_sim::TournamentMode;
using mas_sim::RngKind;
namespace {

int g_failures = 0;
int g_checks = 0;

void report(bool ok, const char* name) {
  ++g_checks;
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) ++g_failures;
}

// Predict a single zoo_draw_uniform() value after fast_rng().reseed(seed)
// via a throwaway Rng of the same seed.
double predict_uniform(int seed) {
  Rng lr(seed);
  return lr.uniform();
}

// Crafted-history caller (selectAction takes non-const refs; we copy).
int call(mas_sim::AgentBase* a, int n, const std::vector<int>& my,
         const std::vector<int>& o1, const std::vector<int>& o2) {
  std::vector<int> m(my), a1(o1), a2(o2);
  return a->selectAction(n, m, a1, a2);
}

// Convenience: n identical entries (0/1).
std::vector<int> rep(int n, int v) { return std::vector<int>(n, v); }

// =============================================================================
// (i) scripted-history unit tests.
// =============================================================================
void test_unit_deterministic() {
  std::printf("--- scripted-history unit tests (Tier-3, deterministic) ---\n");

  // 2. CounterProber: probe script P,P,W,W,P,W,P,P on rds 0..7; then the
  //    three per-opponent classification branches (OR-combined).
  {
    auto a = Registry::instance().get("CounterProber")();
    const std::vector<int> p2w = {0, 1, 0, 1, 0, 1, 0, 1};   // exact period-2
    const std::vector<int> np1 = {0, 0, 0, 1, 0, 0, 1, 0};   // non-periodic
    const std::vector<int> np2 = {1, 0, 1, 0, 1, 0, 1, 1};   // non-periodic
    const bool ok =
        // --- probe script rds 0..7 ---
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 1, {0}, {0}, {0}) == 0 &&
        call(a.get(), 2, {0, 0}, {0, 0}, {0, 0}) == 1 &&
        call(a.get(), 3, {0, 0, 1}, {0, 0, 0}, {0, 0, 0}) == 1 &&
        call(a.get(), 4, {0, 0, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}) == 0 &&
        call(a.get(), 5, {0, 0, 1, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}) ==
            1 &&
        call(a.get(), 6, {0, 0, 1, 1, 0, 1}, rep(6, 0), rep(6, 0)) == 0 &&
        call(a.get(), 7, {0, 0, 1, 1, 0, 1, 0}, rep(7, 0), rep(7, 0)) == 0 &&
        // --- Branch A: opp1 = period-2 (lock), opp2 = all-peace (saint).
        //    Lock: k even -> predict m[0]=0 -> war; k odd -> predict
        //    m[1]=1 -> peace.  Saint: always war.  OR is always war. ---
        call(a.get(), 8, rep(8, 0), p2w, rep(8, 0)) == 1 &&
        call(a.get(), 9, rep(9, 0), p2w, rep(9, 0)) == 1 &&
        call(a.get(), 10, rep(10, 0), p2w, rep(10, 0)) == 1 &&
        call(a.get(), 11, rep(11, 0), p2w, rep(11, 0)) == 1 &&
        // --- Branch B: both all-peace -> saint-exploit -> war. ---
        call(a.get(), 8, rep(8, 0), rep(8, 0), rep(8, 0)) == 1 &&
        // --- Branch C: both non-periodic, non-saint -> TFT_any.
        //    At n=8: o1[7]=0, o2[7]=1 -> OR -> 1.
        //    At n=9 (extend): o1[8]=0, o2[8]=0 -> OR -> 0.
        //    At n=10 (extend): o1[9]=1, o2[9]=0 -> OR -> 1. ---
        call(a.get(), 8, rep(8, 0), np1, np2) == 1 &&
        call(a.get(), 9, rep(9, 0), {0, 0, 0, 1, 0, 0, 1, 0, 0},
             {1, 0, 1, 0, 1, 0, 1, 1, 0}) == 0 &&
        call(a.get(), 10, rep(10, 0), {0, 0, 0, 1, 0, 0, 1, 0, 0, 1},
             {1, 0, 1, 0, 1, 0, 1, 1, 0, 0}) == 1;
    report(ok,
           "CounterProber: probe script; period-2 lock / saint / TFT_any");
  }

  // 2b. CounterProber pure-lock isolation: BOTH windows exact period-2
  //     (0,1,...) -> the OR reduces to the pure inverse-phase lock.
  {
    auto a = Registry::instance().get("CounterProber")();
    const std::vector<int> p2 = {0, 1, 0, 1, 0, 1, 0, 1};
    const bool ok =
        call(a.get(), 8, rep(8, 0), p2, p2) == 1 &&   // k%2=0 -> W
        call(a.get(), 9, rep(9, 0), p2, p2) == 0 &&   // k%2=1 -> P
        call(a.get(), 10, rep(10, 0), p2, p2) == 1 &&
        call(a.get(), 11, rep(11, 0), p2, p2) == 0;
    report(ok, "CounterProber: pure period-2 lock inverts both phases");
  }

  // 4. ColludeSqueeze: n==0 plus all 4 last-round combinations.
  {
    auto a = Registry::instance().get("ColludeSqueeze")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 1 &&         // n==0 -> war
        call(a.get(), 1, {0}, {0}, {0}) == 1 &&      // (P,P) -> war
        call(a.get(), 1, {0}, {1}, {0}) == 0 &&      // (W,P) -> peace
        call(a.get(), 1, {0}, {0}, {1}) == 0 &&      // (P,W) -> peace
        call(a.get(), 1, {0}, {1}, {1}) == 0;        // (W,W) -> peace
    report(ok, "ColludeSqueeze: n==0 war; (P,P)->war, else peace (4 combos)");
  }

  // 5. MetaV1: deterministic branches (fresh instance per sub-case).
  {
    auto f = Registry::instance().get("MetaV1");
    // 5a: fresh, all-peace: pre-probe 0, W probe at rd 5, gap 0 at rd 7.
    const int a1 = call(f().get(), 0, {}, {}, {});
    const int a2 = call(f().get(), 4, rep(4, 0), rep(4, 0), rep(4, 0));
    const int a3 = call(f().get(), 5, rep(5, 0), rep(5, 0), rep(5, 0));
    const int a4 = call(f().get(), 7, rep(7, 0), rep(7, 0), rep(7, 0));
    report(a1 == 0 && a2 == 0 && a3 == 1 && a4 == 0,
           "MetaV1: pre-probe 0, probe W@5, gap 0 (deterministic path)");
    // 5b: retaliator branch (an opp war in rds 0..13): TFT_any.
    const int b1 =
        call(f().get(), 14, rep(14, 0),
             {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, rep(14, 0));
    const int b2 = call(f().get(), 14, rep(14, 0),
                        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
                        rep(14, 0));
    report(b1 == 0 && b2 == 1, "MetaV1: probe-retaliators -> TFT_any");
    // 5c: sustained-squeeze commit. Single instance: at n=6 the last 5
    //     completed rds (1..5) are all my war with NO both-opp round
    //     (o1 all-peace, o2 all-peace) -> commit. At n=11 (same instance)
    //     the commit is permanent even though rds 9-10 had both-opp wars.
    {
      auto a = f();
      const std::vector<int> my6 = {0, 1, 1, 1, 1, 1};
      const std::vector<int> o16 = rep(6, 0);
      const std::vector<int> o26 = rep(6, 0);
      const int c1 = call(a.get(), 6, my6, o16, o26);  // commit -> 1
      const std::vector<int> my11 = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
      const std::vector<int> o111 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1};
      const std::vector<int> o211 = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1};
      const int c2 = call(a.get(), 11, my11, o111, o211);  // permanent -> 1
      report(c1 == 1 && c2 == 1,
             "MetaV1: 5-war streak w/o both-opp -> commit, permanent");
    }
    // 5d: a BOTH-opp retaliation in the last-5 window blocks the commit.
    {
      auto a = f();
      const std::vector<int> my6 = {0, 1, 1, 1, 1, 1};
      // o1: W at rds 3,4,5; o2: W at rds 4,5 → both W at rds 4,5
      const std::vector<int> o16 = {0, 0, 0, 1, 1, 1};
      const std::vector<int> o26 = {0, 0, 0, 0, 1, 1};
      const int d1 = call(a.get(), 6, my6, o16, o26);  // n=6 → gap → 0
      report(d1 == 0,
             "MetaV1: both-opp retaliation in window blocks the commit");
    }
  }
}

void test_unit_stochastic() {
  std::printf(
      "--- scripted-history unit tests (Tier-3, stochastic, seeded) ---\n");

  // 1. ZDExtort3P: p(peace) 0.20/0.55/0.85 by last-round opp war-count
  //     (one uniform per call, predicted from a reseeded throwaway Rng).
  {
    auto a = Registry::instance().get("ZDExtort3P")();
    // n==0: opens war (no draw consumed).
    const int r0 = call(a.get(), 0, {}, {}, {});
    // c=0 (both peace) -> p(peace) = 0.20
    const double u0 = predict_uniform(100001);
    mas_sim::fast_rng().reseed(100001);
    const int r1 = call(a.get(), 1, {0}, {0}, {0});
    // c=1 (one opp war) -> p(peace) = 0.55
    const double u1 = predict_uniform(100002);
    mas_sim::fast_rng().reseed(100002);
    const int r2 = call(a.get(), 1, {0}, {1}, {0});
    // c=2 (both war) -> p(peace) = 0.85
    const double u2 = predict_uniform(100003);
    mas_sim::fast_rng().reseed(100003);
    const int r3 = call(a.get(), 1, {0}, {1}, {1});
    report(r0 == 1 && r1 == (u0 < 0.20 ? 0 : 1) && r2 == (u1 < 0.55 ? 0 : 1) &&
           r3 == (u2 < 0.85 ? 0 : 1),
           "ZDExtort3P: opens war; p(peace) 0.20/0.55/0.85 (seeded)");
  }

  // 3. AdaptModel5: fallback and learned branches.
  {
    auto f = Registry::instance().get("AdaptModel5");
    // Fallback: n=3 -> 2 obs in bucket (0,0) < 8 -> p=0.5 -> joint 0.25
    //   < 0.45 -> war.
    const int r0 = call(f().get(), 3, rep(3, 0), rep(3, 0), rep(3, 0));
    // Learned branch A: n=10, I warred rds 1..9, both opps peace.
    //   Ingest i=1..9: i=1 → ctx(0,0), i=2..9 → ctx(1,0) × 8.
    //   Current ctx (1,0): 8 obs, all my-following-war -> p=1.0.
    //   joint = 0 < 0.45 -> war.
    const std::vector<int> myA = {0, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    const int r1 = call(f().get(), 10, myA, rep(10, 0), rep(10, 0));
    // Learned branch B: n=10, I peace all rds, both opps peace.
    //   Ingest i=1..9: all ctx(0,0), v=0 → 9 obs in (0,0).
    //   Current ctx (0,0): p=0/9=0.0. joint=1.0 >= 0.45 -> peace.
    const int r2 = call(f().get(), 10, rep(10, 0), rep(10, 0), rep(10, 0));
    // n==0 guard: no my last action (m0=0), no data (cc=0 < 8)
    //   -> p=0.5 -> joint 0.25 < 0.45 -> war.
    const int r3 = call(f().get(), 0, {}, {}, {});
    report(r0 == 1 && r1 == 1 && r2 == 0 && r3 == 1,
           "AdaptModel5: fallback->war; learned(1,0)->war; "
           "learned(0,0)->peace; n==0->war");
  }

  // 5. MetaV1 burst branch (n=11..13, rate 0.6): one draw per call.
  {
    auto a = Registry::instance().get("MetaV1")();
    mas_sim::fast_rng().reseed(300001);
    const int r11 = call(a.get(), 11, rep(11, 0), rep(11, 0), rep(11, 0));
    const int r12 = call(a.get(), 12, rep(12, 0), rep(12, 0), rep(12, 0));
    const int r13 = call(a.get(), 13, rep(13, 0), rep(13, 0), rep(13, 0));
    Rng lr(300001);
    const bool ok = (r11 == 1) == (lr.uniform() < 0.6) &&
                    (r12 == 1) == (lr.uniform() < 0.6) &&
                    (r13 == 1) == (lr.uniform() < 0.6);
    report(ok, "MetaV1: burst rds 11..13 war at rate 0.6 (seed 300001)");
  }

  // 5. MetaV1 farming branch (n=14, non-retaliators, rate 0.35).
  {
    auto a = Registry::instance().get("MetaV1")();
    const double u = predict_uniform(300002);
    mas_sim::fast_rng().reseed(300002);
    const int r = call(a.get(), 14, rep(14, 0), rep(14, 0), rep(14, 0));
    report(r == (u < 0.35 ? 1 : 0),
           "MetaV1: farming branch war iff uniform < 0.35 (seed 300002)");
  }
}

// =============================================================================
// (ii) smoke tournament.
// =============================================================================
bool finite_scores(const std::vector<double>& s) {
  if (s.empty()) return false;
  for (double v : s)
    if (!std::isfinite(v)) return false;
  return true;
}

void test_smoke_tournament() {
  std::printf("--- smoke tournament (5 Tier-3 + 6 Tier-0 anchors, 5 runs) ---\n");

  const std::vector<std::string> t3 = {"ZDExtort3P", "CounterProber",
                                       "AdaptModel5", "ColludeSqueeze",
                                       "MetaV1"};
  const std::vector<std::string> anchors = {"NiceAgent", "NastyAgent",
                                            "RandomAgent", "TolerantAgent",
                                            "FreakyAgent", "T4TAgent"};

  // All 5 Tier-3 names present in this build.
  bool all_registered = true;
  for (const auto& n : t3)
    if (!Registry::instance().has(n)) all_registered = false;
  report(all_registered, "all 5 Tier-3 names registered");

  std::vector<mas_sim::AgentFactory> factories;
  factories.reserve(t3.size() + anchors.size());
  for (const auto& n : t3) factories.push_back(Registry::instance().get(n));
  for (const auto& n : anchors)
    factories.push_back(Registry::instance().get(n));
  const int N = static_cast<int>(factories.size());  // 11

  bool no_throw = true, all_finite = true;
  for (int r = 0; r < 5; ++r) {
    try {
      const auto s =
          run_tournament(factories, N, 6000 + r, TournamentMode::kDistinct,
                         RngKind::kSfc64);
      if (!finite_scores(s)) all_finite = false;
    } catch (const std::exception& e) {
      std::printf("      run %d threw: %s\n", r, e.what());
      no_throw = false;
      all_finite = false;
    }
  }
  report(no_throw, "smoke tournament: no exceptions across 5 runs");
  report(all_finite, "smoke tournament: all scores finite");

  // Same-seed determinism.
  {
    const auto a =
        run_tournament(factories, N, 987655, TournamentMode::kDistinct,
                      RngKind::kSfc64);
    const auto b =
        run_tournament(factories, N, 987655, TournamentMode::kDistinct,
                      RngKind::kSfc64);
    report(a == b && finite_scores(a),
           "smoke tournament: same seed => identical score vector");
  }
}

// =============================================================================
// (iii) sanity checks.
// =============================================================================
void test_sanity() {
  std::printf("--- sanity checks ---\n");

  // (a) ColludeSqueeze vs {Nice, Nice} >= 5.5/rd.
  //   rd 0: (P,P,P) -> 1. rds 1..R-1: (W,P,P) -> 6/rd.
  //   avg = (1 + 6(R-1))/R = 6 - 5/R in [5.955, 5.994] for R in [90,110].
  {
    const auto cs = Registry::instance().get("ColludeSqueeze");
    const auto nice = Registry::instance().get("NiceAgent");
    double avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = cs;
      f[1] = nice;
      f[2] = nice;
      const auto s =
          run_tournament(f, 3, 43000 + r, TournamentMode::kDistinct,
                         RngKind::kSfc64);
      avg += s[0];
    }
    avg /= kRuns;
    std::printf("      ColludeSqueeze vs Nice-pair = %.4f/rd\n", avg);
    report(avg >= 5.5, "ColludeSqueeze vs Nice-pair >= 5.5/rd");
  }

  // (b) ZDExtort3P vs {Nasty, Nasty} >= 1.5/rd: the dispatch asks for
  //     >= 1.5/rd, but the LITERAL reading cannot satisfy it — flag:
  //     vs two pure-Nasty players every round is (x,1,1). The per-round
  //     payoff is 0 for war and -1 for peace (payoff[1][1][1]=0,
  //     payoff[0][1][1]=-1), so the per-round ceiling is 0. The bot's
  //     long-run value is ~ -p(peace) in [-1, 0] < 1.5. Same class of
  //     flag as B2's Gradual3P-vs-Nasty-pair. The test asserts the true
  //     verifiable bounds and flags the literal direction.
  {
    const auto z = Registry::instance().get("ZDExtort3P");
    const auto nasty = Registry::instance().get("NastyAgent");
    double avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = z;
      f[1] = nasty;
      f[2] = nasty;
      const auto s =
          run_tournament(f, 3, 44000 + r, TournamentMode::kDistinct,
                         RngKind::kSfc64);
      avg += s[0];
    }
    avg /= kRuns;
    std::printf("      ZDExtort3P vs Nasty-pair = %.4f/rd (literal "
                "'>= 1.5' unsatisfiable; per-round ceiling vs (1,1) is 0)\n",
                avg);
    report(std::isfinite(avg) && avg < 0.5 && avg > -1.0,
           "ZDExtort3P vs Nasty-pair: finite, < 0.5, > -1 "
           "(literal '>= 1.5' FLAGGED as false)");
  }

  // (c) CounterProber vs Alternator: the dispatch asks for "> 3.5/rd
  //     (period-2 lock works)". Flag: the 3-way match has two opponent
  //     slots; the literal {Alternator, Alternator} pair is unsatisfiable
  //     — the lock inverts the Alternator phase, producing (0,0,0)=4 on
  //     even rounds and (1,1,1)=0 on odd rounds, so the per-round ceiling
  //     is 3.0/rd and the perfect-lock average is 3.0 - 1/R in
  //     [2.955, 2.999] < 3.5. The operational pair {Alternator,
  //     NiceAgent} is where the lock + saint-exploit pays: from n=8 the
  //     bot plays W every round (the all-peace Nice branch says war,
  //     overriding the lock's peace phases); even rounds are (W,P,P)=6,
  //     odd rounds are (W,W,P)=3, averaging ~4.5/rd; with the 8-round
  //     probe prefix the full-match average is ~4.4/rd > 3.5.
  {
    const auto cp = Registry::instance().get("CounterProber");
    const auto alt = Registry::instance().get("Alternator");
    const auto nice = Registry::instance().get("NiceAgent");
    double avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = cp;
      f[1] = alt;
      f[2] = nice;
      const auto s =
          run_tournament(f, 3, 45000 + r, TournamentMode::kDistinct,
                         RngKind::kSfc64);
      avg += s[0];
    }
    avg /= kRuns;
    std::printf("      CounterProber vs {Alternator, NiceAgent} = %.4f/rd "
                "(literal {Alt, Alt} ceiling is 3.0/rd — FLAGGED; "
                "operational pair asserted)\n",
                avg);
    report(avg > 3.5,
           "CounterProber vs {Alternator, NiceAgent} > 3.5/rd "
           "(period-2 lock + saint-exploit work)");
  }
}

}  // namespace

int main() {
  std::printf("=== zoo_t3_test ===\n");
  test_unit_deterministic();
  test_unit_stochastic();
  test_smoke_tournament();
  test_sanity();

  if (g_failures == 0) {
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("FAIL: %d/%d check(s) failed\n", g_failures, g_checks);
  return 1;
}
