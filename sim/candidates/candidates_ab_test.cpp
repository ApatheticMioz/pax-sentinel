// =============================================================================
// sim/candidates/candidates_ab_test.cpp — slice C1a: behavioral tests for
// candidate CandA_GTFT and CandB_KLenient.
//
// Mirrors zoo_t2_test.cpp conventions:
//   (i)  scripted-history unit tests: drive selectAction() directly with
//        crafted histories (deterministic g/eps=0/1 variants + seeded
//        draw-prediction via a throwaway Rng of the same seed);
//   (ii) smoke tournament: CandA + CandB + 6 zoo anchors, 5 runs, fast mode,
//        distinct enumeration; no exceptions, finite scores, same-seed
//        determinism;
//   (iii) sanity checks (with flagged-literal readings where the dispatch's
//        numbers are arithmetically unsatisfiable — see the dispatch flag).
//
// Round-index convention: n == number of completed rounds; the histories
// hold rds 0..n-1; "rd k" is the 0-based index k. CandA's isolation window
// "rds n-4..n-2" (the 3 rounds before the last, under that convention) is
// implemented as indices [n-4, n-1) clamped to 0 — 4 rounds back from the
// last, matching the zoo's 0-based window readings.
//
// Build: sim/build.sh `candidates` / `candidatesasan` (this file is the
// test main; it links engine.cpp + zoo_t01.cpp + candidates_ab.cpp).
// =============================================================================

#include "../candidates/candidates_ab.hpp"
#include "../zoo/zoo_common.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using mas_sim::AgentBase;
using mas_sim::CandAParams;
using mas_sim::CandBParams;
using mas_sim::Rng;
using mas_sim::RngKind;
using mas_sim::Registry;
using mas_sim::TournamentMode;
using mas_sim::make_CandA_GTFT;
using mas_sim::make_CandB_KLenient;
using mas_sim::run_tournament;

namespace {

int g_failures = 0;
int g_checks = 0;

void report(bool ok, const char* name) {
  ++g_checks;
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) ++g_failures;
}

// Predict fast_rng().uniform() for a freshly-reseeded fast_rng(seed) via a
// throwaway Rng of the same seed (zoo test convention).
double predict_uniform(int seed) { return Rng(seed).uniform(); }

// Crafted-history caller (selectAction takes non-const refs; we copy).
int call(mas_sim::AgentBase* a, int n, const std::vector<int>& my,
         const std::vector<int>& o1, const std::vector<int>& o2) {
  std::vector<int> m(my), a1(o1), a2(o2);
  return a->selectAction(n, m, a1, a2);
}

std::vector<int> rep(int n, int v) { return std::vector<int>(n, v); }

bool finite_scores(const std::vector<double>& s) {
  if (s.empty()) return false;
  for (double v : s)
    if (!std::isfinite(v)) return false;
  return true;
}

}  // namespace

// =============================================================================
// (i-a) CandA scripted-history unit tests (10 scenarios).
// =============================================================================
void test_candA_unit() {
  std::printf("--- CandA_GTFT scripted-history unit tests ---\n");

  // A1. n==0 -> open peace (both g variants).
  {
    auto g0 = make_CandA_GTFT(CandAParams{0.0})();
    auto g1 = make_CandA_GTFT(CandAParams{1.0})();
    report(call(g0.get(), 0, {}, {}, {}) == 0 &&
               call(g1.get(), 0, {}, {}, {}) == 0,
           "A1: n==0 -> peace (both g variants)");
  }

  // A2. both peace last rd -> peace (no draws, any g).
  {
    auto a = make_CandA_GTFT()();  // default g=0.5
    report(call(a.get(), 5, rep(5, 0), rep(5, 0), rep(5, 0)) == 0,
           "A2: both-peace -> peace");
  }

  // A3. one-war ISOLATED, g=1 (deterministic forgive) -> peace.
  //     n=5: o1 warred only at rd 4 (last); window [1,5) has 0 wars.
  {
    auto a = make_CandA_GTFT(CandAParams{1.0})();
    report(call(a.get(), 5, rep(5, 0), {0, 0, 0, 0, 1}, rep(5, 0)) == 0,
           "A3: isolated single-war, g=1 -> peace");
  }

  // A4. one-war ISOLATED, g=0 (deterministic no-forgive) -> war.
  {
    auto a = make_CandA_GTFT(CandAParams{0.0})();
    report(call(a.get(), 5, rep(5, 0), {0, 0, 0, 0, 1}, rep(5, 0)) == 1,
           "A4: isolated single-war, g=0 -> war");
  }

  // A5. one-war NON-isolated (that opp also warred inside the window)
  //     -> war regardless of g.
  {
    auto a = make_CandA_GTFT(CandAParams{1.0})();  // even g=1 can't forgive
    const bool ok =
        // o1 war at rd 2 (in window [1,5)) and rd 4 -> non-isolated
        call(a.get(), 5, rep(5, 0), {0, 0, 1, 0, 1}, rep(5, 0)) == 1 &&
        // o1 war at rd 1 (in window) and rd 4
        call(a.get(), 5, rep(5, 0), {0, 1, 0, 0, 1}, rep(5, 0)) == 1;
    report(ok, "A5: one-war non-isolated (war in rds n-4..n-2) -> war");
  }

  // A6. BOTH warred last rd -> always war, even with g=1.
  {
    auto a = make_CandA_GTFT(CandAParams{1.0})();
    report(call(a.get(), 5, rep(5, 0), {0, 0, 0, 0, 1}, {0, 0, 0, 0, 1}) == 1,
           "A6: both-war last rd -> war (even g=1)");
  }

  // A7. n=1 boundary: the window before the last is empty -> a single war
  //     at rd 0 is isolated.
  {
    const auto a1 = make_CandA_GTFT(CandAParams{1.0})();
    const auto a0 = make_CandA_GTFT(CandAParams{0.0})();
    report(call(a1.get(), 1, {0}, {0}, {1}) == 0 &&
               call(a0.get(), 1, {0}, {0}, {1}) == 1,
           "A7: n=1 single-war is isolated (g=1 -> peace, g=0 -> war)");
  }

  // A8. the OTHER opp's non-last war must not break isolation.
  {
    auto a = make_CandA_GTFT(CandAParams{1.0})();
    // n=6: o1 warred only at rd 5 (last); its window [2,6) is clean ->
    // isolated; o2 warred at rd 2 (outside the window) but peace last.
    report(call(a.get(), 6, rep(6, 0), {0, 0, 0, 0, 0, 1},
                {0, 0, 1, 0, 0, 0}) == 0,
           "A8: other opp's non-last war does not break isolation");
  }

  // A9. isolation logic applies to opp2 symmetrically.
  {
    auto a = make_CandA_GTFT(CandAParams{1.0})();
    report(call(a.get(), 5, rep(5, 0), rep(5, 0), {0, 0, 0, 0, 1}) == 0 &&
               call(a.get(), 5, rep(5, 0), rep(5, 0), {0, 0, 1, 0, 1}) == 1,
           "A9: opp2 isolated -> forgive; opp2 non-isolated -> war");
  }

  // A10. g=0.5 (default) seeded: isolated single-war, predict the
  //      fast_rng draw.
  {
    auto a = make_CandA_GTFT()();
    const double u = predict_uniform(700001);
    mas_sim::fast_rng().reseed(700001);
    const int r = call(a.get(), 5, rep(5, 0), {0, 0, 0, 0, 1}, rep(5, 0));
    report(r == (u < 0.5 ? 0 : 1),
           "A10: g=0.5 isolated draw (seed 700001)");
  }
}

// =============================================================================
// (i-b) CandB scripted-history unit tests (10 scenarios).
// =============================================================================
void test_candB_unit() {
  std::printf("--- CandB_KLenient scripted-history unit tests ---\n");

  // B1. n==0 -> open peace.
  {
    auto a = make_CandB_KLenient()();
    report(call(a.get(), 0, {}, {}, {}) == 0, "B1: n==0 -> peace");
  }

  // B2. no wars in the window -> peace.
  {
    auto a = make_CandB_KLenient()();  // eps=0.2, strict
    report(call(a.get(), 5, rep(5, 0), rep(5, 0), rep(5, 0)) == 0,
           "B2: 0 wars in window -> peace");
  }

  // B3. isolated (1 war < thresh), eps=1.0 -> deterministic forgive.
  {
    auto a = make_CandB_KLenient(CandBParams{3, 2, 2, 3, 1.0, 0})();
    report(call(a.get(), 3, {0, 0, 0}, {1, 0, 0}, {0, 0, 0}) == 0,
           "B3: 1 war in window, eps=1.0 -> peace (forgiven)");
  }

  // B4. isolated (1 war), eps=0.0 -> NOT forgiven: trigger; then the phase
  //     machine drives: 2nd punish war, then amnesty peace.
  {
    auto a = make_CandB_KLenient(CandBParams{3, 2, 2, 3, 0.0, 0})();
    const bool ok =
        call(a.get(), 3, {0, 0, 0}, {1, 0, 0}, {0, 0, 0}) == 1 &&  // trigger
        call(a.get(), 4, {0, 0, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}) == 1 &&
        call(a.get(), 5, rep(5, 0), rep(5, 0), rep(5, 0)) == 0 &&  // amnesty
        call(a.get(), 6, rep(6, 0), rep(6, 0), rep(6, 0)) == 0;
    report(ok, "B4: eps=0.0 counts isolated war -> punish + amnesty");
  }

  // B5. 2 wars in window (full threshold) -> trigger; punish exactly 2 war
  //     rounds (incl. trigger round), then amnesty 3 (jitter=0) peace;
  //     normal phase re-answers a fresh trigger.
  {
    auto a = make_CandB_KLenient(CandBParams{3, 2, 2, 3, 0.2, 0})();
    const bool ok =
        call(a.get(), 3, {0, 0, 0}, {1, 1, 0}, {0, 0, 0}) == 1 &&  // trigger
        call(a.get(), 4, rep(4, 0), rep(4, 0), rep(4, 0)) == 1 &&   // punish rd2
        call(a.get(), 5, rep(5, 0), rep(5, 0), rep(5, 0)) == 0 &&   // amnesty
        call(a.get(), 6, rep(6, 0), rep(6, 0), rep(6, 0)) == 0 &&
        call(a.get(), 7, rep(7, 0), rep(7, 0), rep(7, 0)) == 0 &&   // amnesty=3
        // normal again: a fresh 2-war window triggers -> war
        call(a.get(), 8, rep(8, 0), {0, 0, 0, 0, 0, 0, 1, 1},
             rep(8, 0)) == 1;
    report(ok, "B5: 2/3 trigger -> 2 war rds + 3-rd amnesty -> normal");
  }

  // B6. amnesty jitter: set the seed, predict the jitter draw with a
  //     throwaway Rng; the amnesty length must be 3 + jitter, i.e. in
  //     {2,3,4}. An eps=0.0 variant is used so the jitter is the ONLY draw
  //     (normal-phase triggers short-circuit without a draw when eps is
  //     degenerate), and the probe histories are crafted so the first
  //     post-amnesty war happens exactly at n = 5+len: with eps=0 any
  //     single war in the window triggers, and the o1 war at index 6 puts
  //     its first war in the last-3 window at n=7 (i.e. len=2 -> war at 7;
  //     len=3/4 -> the 2nd war at index 7 enters the window at n=8/9).
  {
    auto a = make_CandB_KLenient(CandBParams{3, 2, 2, 3, 0.0, 1})();
    // Drive the machine to the punish->amnesty transition at n=5 with NO
    // draws before the seeded one (full-threshold trigger: 2 wars, w=2).
    const int r3 = call(a.get(), 3, {0, 0, 0}, {1, 1, 0}, {0, 0, 0});
    const int r4 = call(a.get(), 4, rep(4, 0), rep(4, 0), rep(4, 0));
    // Seed right before the ONLY draw (the jitter, at the n=5 call).
    const double u = predict_uniform(700002);
    const int expect_len =
        3 + (u < 1.0 / 3.0 ? -1 : (u < 2.0 / 3.0 ? 0 : 1));
    mas_sim::fast_rng().reseed(700002);
    const int r5 = call(a.get(), 5, rep(5, 0), rep(5, 0), rep(5, 0));  // draws
    // Probe after the amnesty with o1 wars at indices 6 and 7.
    const std::vector<int> o1 = {0, 0, 0, 0, 0, 0, 1, 1, 0, 0};
    int first_war_n = -1;
    for (int n = 6; n <= 9; ++n) {
      std::vector<int> m(n, 0), o1p(o1.begin(), o1.begin() + n), o2p(n, 0);
      if (a->selectAction(n, m, o1p, o2p) == 1) {
        first_war_n = n;
        break;
      }
    }
    const int observed_len = first_war_n - 5;
    std::printf("      (amnesty length: predicted=%d observed=%d)\n",
                expect_len, observed_len);
    report(r3 == 1 && r4 == 1 && r5 == 0 && expect_len >= 2 &&
               expect_len <= 4 && observed_len == expect_len,
           "B6: amnesty jittered -> length in {2,3,4}, matches seed");
  }

  // B7. trigger via opp2 (opp1 clean) -> war.
  {
    auto a = make_CandB_KLenient(CandBParams{3, 2, 2, 3, 0.2, 0})();
    report(call(a.get(), 3, {0, 0, 0}, {0, 0, 0}, {1, 1, 0}) == 1,
           "B7: 2 wars in opp2 window -> trigger");
  }

  // B8. window clamping: n=2 < window=3, both available rds warred ->
  //     2 >= thresh -> trigger.
  {
    auto a = make_CandB_KLenient(CandBParams{3, 2, 2, 3, 0.2, 0})();
    report(call(a.get(), 2, {0, 0}, {1, 1}, {0, 0}) == 1,
           "B8: short-history window clamps; 2/2 -> trigger");
  }

  // B9. machine overrides history: during punish (even all-peace history)
  //     the bot keeps warring; during amnesty it stays peace even though
  //     the window still shows 2 wars (n=5 probe: last-3 = {1,1,0}).
  {
    auto a = make_CandB_KLenient(CandBParams{3, 2, 2, 3, 0.2, 0})();
    const bool ok =
        call(a.get(), 3, {0, 0, 0}, {1, 1, 0}, {0, 0, 0}) == 1 &&  // trigger
        call(a.get(), 4, rep(4, 0), rep(4, 0), rep(4, 0)) == 1 &&  // punish
        call(a.get(), 5, rep(5, 0), {1, 1, 0, 0, 0},
             {0, 0, 0, 0, 0}) == 0 &&  // amnesty overrides 2-war window
        call(a.get(), 6, rep(6, 0), {1, 1, 0, 0, 0, 0},
             {0, 0, 0, 0, 0, 0}) == 0;
    report(ok, "B9: punish/amnesty machine overrides history");
  }

  // B10. eps=0.2 seeded: isolated (1 war) -> forgive iff u < 0.2.
  {
    auto a = make_CandB_KLenient()();  // default eps=0.2, jitter=1
    const double u = predict_uniform(700003);
    mas_sim::fast_rng().reseed(700003);
    const int r = call(a.get(), 3, {0, 0, 0}, {1, 0, 0}, {0, 0, 0});
    report(r == (u >= 0.2 ? 1 : 0),
           "B10: eps=0.2 isolated war, forgive iff u<0.2 (seed 700003)");
  }
}

// =============================================================================
// (ii) smoke tournament: CandA + CandB + 6 zoo anchors, 5 runs.
// =============================================================================
void test_smoke_tournament() {
  std::printf("--- smoke tournament (CandA + CandB + 6 anchors, 5 runs) ---\n");

  const std::vector<std::string> anchors = {
      "NiceAgent", "NastyAgent", "RandomAgent",
      "T4TAgent",  "TolerantAgent", "TFT_any"};

  bool all_registered = true;
  for (const auto& n : anchors)
    if (!Registry::instance().has(n)) all_registered = false;
  report(all_registered && Registry::instance().has("CandA_GTFT") &&
             Registry::instance().has("CandB_KLenient"),
         "all 8 smoke names registered");

  std::vector<mas_sim::AgentFactory> factories;
  factories.reserve(8);
  factories.push_back(Registry::instance().get("CandA_GTFT"));
  factories.push_back(Registry::instance().get("CandB_KLenient"));
  for (const auto& n : anchors)
    factories.push_back(Registry::instance().get(n));
  const int N = static_cast<int>(factories.size());  // 8

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
  report(no_throw, "smoke: no exceptions across 5 runs");
  report(all_finite, "smoke: all scores finite");

  {
    const auto a =
        run_tournament(factories, N, 987777, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    const auto b =
        run_tournament(factories, N, 987777, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    report(a == b && finite_scores(a),
           "smoke: same seed => identical score vector");
  }
}

// =============================================================================
// (iii) sanity checks (5-run averages, fast mode, distinct).
// =============================================================================
void test_sanity() {
  std::printf("--- sanity checks ---\n");

  // (a) CandA vs {NiceAgent, TolerantAgent} >= 3.8/rd.
  //     CandA never initiates; Tolerant only wars when the opps' cumulative
  //     war count EXCEEDS their peace count, which never happens against
  //     {mostly-peace CandA, always-peace Nice} -> both opponents play
  //     peace -> CandA scores 4/rd (>= 3.8).
  {
    const auto a = Registry::instance().get("CandA_GTFT");
    const auto nice = Registry::instance().get("NiceAgent");
    const auto tol = Registry::instance().get("TolerantAgent");
    double avg = 0.0;
    for (int r = 0; r < 5; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = a;
      f[1] = nice;
      f[2] = tol;
      avg += run_tournament(f, 3, 61000 + r, TournamentMode::kDistinct,
                            RngKind::kSfc64)[0];
    }
    avg /= 5;
    std::printf("      CandA vs {Nice, Tolerant} = %.4f/rd\n", avg);
    report(std::isfinite(avg) && avg >= 3.8,
           "CandA vs {Nice, Tolerant} >= 3.8/rd");
  }

  // (b) CandB vs {NastyAgent, NastyAgent}.
  //     LITERAL dispatch range [-0.5, 0.1]/rd is UNSATISFIABLE (flagged per
  //     the B2/B3a precedent): against two pure-war opponents the window
  //     holds 3 wars every round, so CandB cycles 2 punish war rounds
  //     (payoff[1][1][1]=0) + K = 3+jitter in {2,3,4} peace rounds
  //     (payoff[0][1][1]=-1); renewal-reward rate E[-K]/E[2+K] = -3/5 =
  //     -0.6/rd (per-match values in {-0.5, -0.6, -0.667}). The only
  //     reading that hits [-0.5, 0.1] (punish = 2 EXTRA war rounds,
  //     i.e. -0.5/rd) contradicts the zoo KLenient convention the dispatch
  //     itself cites as the model. We assert the true band instead.
  {
    const auto b = Registry::instance().get("CandB_KLenient");
    const auto nasty = Registry::instance().get("NastyAgent");
    double avg = 0.0;
    for (int r = 0; r < 5; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = b;
      f[1] = nasty;
      f[2] = nasty;
      avg += run_tournament(f, 3, 62000 + r, TournamentMode::kDistinct,
                            RngKind::kSfc64)[0];
    }
    avg /= 5;
    std::printf("      CandB vs {Nasty, Nasty} = %.4f/rd "
                "(literal [-0.5, 0.1] FLAGGED unsatisfiable; E=-0.6/rd)\n",
                avg);
    report(std::isfinite(avg) && avg >= -0.75 && avg <= -0.4,
           "CandB vs {Nasty, Nasty}: finite, in [-0.75, -0.4] "
           "(true band around -0.6; literal range FLAGGED)");
  }

  // (c) CandA vs {NastyAgent, NastyAgent}.
  //     LITERAL dispatch bound ">= 2.5/rd" is ARITHMETICALLY IMPOSSIBLE
  //     (flagged): CandA's war response vs two war-forever opponents is
  //     permanent war, and the per-round ceiling with two war opponents is
  //     payoff[1][1][1]=0 (rd 0 peace gives -1; avg = -1/R in
  //     [-0.0111, -0.0091]). A >=2.5 magnitude is only reachable against
  //     mostly-peace opponents (see check (a): 4.0/rd). We assert the true
  //     value plus a meaningful substitute: CandA (~0/rd) out-survives
  //     NiceAgent (-1/rd) on the same pair — the "not a sucker" property
  //     the war response is supposed to buy.
  {
    const auto a = Registry::instance().get("CandA_GTFT");
    const auto nice = Registry::instance().get("NiceAgent");
    const auto nasty = Registry::instance().get("NastyAgent");
    double avg_a = 0.0, avg_n = 0.0;
    for (int r = 0; r < 5; ++r) {
      std::vector<mas_sim::AgentFactory> fa(3);
      fa[0] = a;
      fa[1] = nasty;
      fa[2] = nasty;
      avg_a += run_tournament(fa, 3, 63000 + r, TournamentMode::kDistinct,
                              RngKind::kSfc64)[0];
      std::vector<mas_sim::AgentFactory> fn(3);
      fn[0] = nice;
      fn[1] = nasty;
      fn[2] = nasty;
      avg_n += run_tournament(fn, 3, 64000 + r, TournamentMode::kDistinct,
                              RngKind::kSfc64)[0];
    }
    avg_a /= 5;
    avg_n /= 5;
    std::printf("      CandA vs {Nasty, Nasty} = %.4f/rd (ceiling 0; "
                "literal '>= 2.5' FLAGGED impossible)\n",
                avg_a);
    std::printf("      NiceAgent vs {Nasty, Nasty} = %.4f/rd (baseline)\n",
                avg_n);
    report(std::isfinite(avg_a) && avg_a >= -0.02 && avg_a <= 0.0 &&
               std::isfinite(avg_n) && avg_n <= -0.9 && avg_a > avg_n,
           "CandA vs {Nasty, Nasty}: ~0/rd (true value), and out-survives "
           "NiceAgent; literal '>= 2.5/rd' FLAGGED");
  }
}

int main() {
  std::printf("=== candidates_ab_test (slice C1a: CandA_GTFT, "
              "CandB_KLenient) ===\n");
  test_candA_unit();
  test_candB_unit();
  test_smoke_tournament();
  test_sanity();

  if (g_failures == 0) {
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("FAIL: %d/%d check(s) failed\n", g_failures, g_checks);
  return 1;
}
