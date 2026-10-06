// =============================================================================
// sim/zoo/zoo_t2_test.cpp — slice B3a: behavioral tests for the Tier-2 zoo
//
// Mirrors zoo_t01_test.cpp conventions exactly:
//   (i)  scripted-history unit tests: build a bot via the Registry and call
//        selectAction() with crafted myHistory/opp1/opp2 vectors, asserting
//        the exact expected action. Stateful bots (the "3P" / learner /
//        exploit families) are driven through a consistent, growing history
//        on one agent instance; stochastic bots are made deterministic by
//        reseeding the engine's fast_rng() and predicting the draw with a
//        throwaway Rng of the same seed.
//   (ii) smoke tournament: the 16 Tier-2 bots + 4 Tier-0 anchors, 5 runs,
//        fast mode, distinct enumeration; assert all scores finite, no
//        exceptions, and same-seed determinism.
//   (iii) sanity checks: SaintHunter vs {Nice,Nice}; Gradual3P vs
//        {Nasty,Nasty}; AntiExploit vs {Nasty,Nasty}.
//
// Build: sim/build.sh `zoo` (links engine.cpp + zoo_t01.cpp + zoo_t2.cpp).
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

// Predict zoo_draw_uniform() / zoo_draw_bit() for a freshly-reseeded
// fast_rng(seed) via a throwaway Rng of the same seed.
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
  std::printf("--- scripted-history unit tests (Tier-2, deterministic) ---\n");

  // 1. Prober_DDCC: probe P,P,W,P on rds 0..3; post-probe classification.
  {
    auto a = Registry::instance().get("Prober_DDCC")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 2, rep(2, 0), rep(2, 0), rep(2, 0)) == 1 &&  // rd2 probe W
        call(a.get(), 3, rep(3, 0), rep(3, 0), rep(3, 0)) == 0 &&  // rd3 P
        // both opps all-peace through the 4-round probe -> permanent war
        call(a.get(), 4, rep(4, 0), rep(4, 0), rep(4, 0)) == 1 &&
        // opp1 warred 2 in the probe -> TFT_any (both last moves peace -> 0)
        call(a.get(), 5, rep(5, 0), {1, 1, 0, 0, 0}, rep(5, 0)) == 0;
    report(ok, "Prober_DDCC: probe P,P,W,P; saint->war, >=2->TFT");
  }

  // 2. SaintHunter: peace 0..14; commit war if no opp war in that window.
  {
    auto a = Registry::instance().get("SaintHunter")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 14, rep(14, 0), rep(14, 0), rep(14, 0)) == 0 &&
        // clean commitment at rd 15 -> war
        call(a.get(), 15, rep(15, 0), rep(15, 0), rep(15, 0)) == 1 &&
        call(a.get(), 20, rep(20, 0), rep(20, 0), rep(20, 0)) == 1 &&
        // committed, but an opp warred after the commit -> revert to TFT
        // (last moves peace here -> 0)
        call(a.get(), 20, rep(20, 0), rep(20, 0), {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                  0, 0, 0, 0, 0, 0, 1, 0, 0, 0}) == 0 &&
        // no clean commitment (opp warred in the window) -> TFT_any
        // (last move war -> 1)
        call(a.get(), 16, rep(16, 0), {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0, 0, 1}, rep(16, 0)) == 1;
    report(ok, "SaintHunter: peace 0..14, commit war, revert/TFT");
  }

  // 3. Gradual3P: escalating punishment on a driven history.
  //    o1 provokes at rd1 and rd5; expect war runs of length 1 then 2, each
  //    followed by a 2-round peace amnesty.
  {
    auto a = Registry::instance().get("Gradual3P")();
    const std::vector<int> o1f = {0, 1, 0, 0, 0, 1, 0, 0, 0};
    const std::vector<int> o2f = rep(9, 0);
    const std::vector<int> myf = rep(9, 0);
    const int expect[9] = {0, 0, 1, 0, 0, 0, 1, 1, 0};
    bool ok = true;
    for (int n = 0; n <= 8; ++n) {
      std::vector<int> m(myf.begin(), myf.begin() + n);
      std::vector<int> o1(o1f.begin(), o1f.begin() + n);
      std::vector<int> o2(o2f.begin(), o2f.begin() + n);
      if (a->selectAction(n, m, o1, o2) != expect[n]) ok = false;
    }
    report(ok, "Gradual3P: escalating war runs + 2-rd amnesty");
  }

  // 4. CTFT3P: contrite peace after an unprovoked self-war.
  {
    auto a = Registry::instance().get("CTFT3P")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 1, {0}, {1}, {0}) == 1 &&  // opp warred -> retaliate
        call(a.get(), 1, {1}, {0}, {0}) == 0 &&  // I warred unprovoked -> contrite
        call(a.get(), 2, {0, 1}, {0, 0}, {0, 0}) == 0 &&  // contrite (last)
        call(a.get(), 2, {0, 1}, {0, 1}, {0, 0}) == 1;  // provoked -> war
    report(ok, "CTFT3P: contrite skip after unprovoked war");
  }

  // 7. KLenient2: trigger at >=2 opp wars in last 3 -> war 2 rds then amnesty.
  //    Below-threshold war activity (1 < m < 2) would draw the RNG (forgive
  //    w.p. 0.2), so the deterministic test drives the phase machine directly:
  //    a crafted full-threshold window fires the trigger, then the machine
  //    ignores history for the war/amnesty phases.
  {
    auto a = Registry::instance().get("KLenient2")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        // n=3: o1 last-3 = {1,1,0} -> 2 wars -> trigger (war this round)
        call(a.get(), 3, {0, 0, 0}, {1, 1, 0}, {0, 0, 0}) == 1 &&
        // n=4: still in the 2-round punish window
        call(a.get(), 4, {0, 0, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}) == 1 &&
        // n=5: enter amnesty -> peace
        call(a.get(), 5, {0, 0, 0, 0, 0}, {1, 1, 0, 0, 0},
             {0, 0, 0, 0, 0}) == 0 &&
        // n=6: still amnesty -> peace
        call(a.get(), 6, {0, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0},
             {0, 0, 0, 0, 0, 0}) == 0;
    report(ok, "KLenient2: >=2/3 trigger -> 2 war rds + amnesty");
  }

  // 8. KLenient3: trigger at >=3 opp wars in last 4 -> war 2 rds then amnesty.
  {
    auto a = Registry::instance().get("KLenient3")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        // n=4: o1 last-4 = {1,1,1,0} -> 3 wars -> trigger
        call(a.get(), 4, {0, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}) == 1 &&
        call(a.get(), 5, {0, 0, 0, 0, 0}, {1, 1, 1, 0, 0},
             {0, 0, 0, 0, 0}) == 1 &&
        call(a.get(), 6, {0, 0, 0, 0, 0, 0}, {1, 1, 1, 0, 0, 0},
             {0, 0, 0, 0, 0, 0}) == 0 &&
        call(a.get(), 7, {0, 0, 0, 0, 0, 0, 0}, {1, 1, 1, 0, 0, 0, 0},
             {0, 0, 0, 0, 0, 0, 0}) == 0;
    report(ok, "KLenient3: >=3/4 trigger -> 2 war rds + amnesty");
  }

  // 7b. KLenient2 sub-threshold forgiveness: 1 opp war in the window (1 < m
  //     < 2) -> trigger with p 0.8 (forgive, i.e. stay peace, with p 0.2).
  //     Made deterministic by seeding fast_rng and predicting the draw.
  {
    auto a = Registry::instance().get("KLenient2")();
    const double u = predict_uniform(300001);
    mas_sim::fast_rng().reseed(300001);
    const int r = call(a.get(), 3, {0, 0, 0}, {1, 0, 0}, {0, 0, 0});  // m=1
    const int expect = (u >= 0.2) ? 1 : 0;  // not-forgiven -> war
    report(r == expect, "KLenient2: sub-threshold war, 0.8 trigger (seed)");
  }

  // 9. EndgameGTFT (GTFT part; ramp not reached for n<95).
  {
    auto a = Registry::instance().get("EndgameGTFT")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 1, {0}, {0}, {0}) == 0 &&
        call(a.get(), 2, {0, 0}, {0, 1}, {0, 0}) == 0 &&  // isolated -> forgive
        call(a.get(), 2, {0, 0}, {1, 1}, {0, 0}) == 1 &&  // repeated -> war
        call(a.get(), 3, {0, 0, 0}, {1, 0, 1}, {0, 0, 0}) == 0 &&  // isolated
        call(a.get(), 3, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}) == 1 &&  // repeated
        call(a.get(), 3, {0, 0, 0}, {0, 0, 0}, {0, 0, 1}) == 0;   // isolated
    report(ok, "EndgameGTFT: GTFT one-shot forgiveness (pre-ramp)");
  }

  // 10. HybridA: probe W at rd5; silent->commit war; retaliators->TFT.
  {
    auto a = Registry::instance().get("HybridA")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 4, rep(4, 0), rep(4, 0), rep(4, 0)) == 0 &&
        call(a.get(), 5, rep(5, 0), rep(5, 0), rep(5, 0)) == 1 &&  // probe
        call(a.get(), 7, rep(7, 0), rep(7, 0), rep(7, 0)) == 0 &&  // gap
        // silent non-retaliators -> commit war
        call(a.get(), 14, rep(14, 0), rep(14, 0), rep(14, 0)) == 1 &&
        // retaliators (opp warred in rds 0..13) -> TFT_any (last peace -> 0)
        call(a.get(), 14, rep(14, 0), {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
             rep(14, 0)) == 0;
    report(ok, "HybridA: probe rd5; silent->commit, retaliators->TFT");
  }

  // 11. HybridB: single probe W at rd8; non-retaliators->farming, else TFT.
  {
    auto a = Registry::instance().get("HybridB")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 7, rep(7, 0), rep(7, 0), rep(7, 0)) == 0 &&
        call(a.get(), 8, rep(8, 0), rep(8, 0), rep(8, 0)) == 1 &&  // probe
        // retaliators (opp warred in rds 0..8) -> TFT_any (last peace -> 0)
        call(a.get(), 9, rep(9, 0), {0, 0, 0, 1, 0, 0, 0, 0, 0}, rep(9, 0)) == 0;
    report(ok, "HybridB: probe rd8; retaliators->TFT");
  }

  // 12. FreqLearner: per-opp lifetime rate (fresh agent per case).
  {
    auto f = Registry::instance().get("FreqLearner");
    const bool ok =
        call(f().get(), 1, rep(1, 0), {0}, {0}) == 0 &&           // avg 0
        call(f().get(), 2, rep(2, 0), {1, 1}, {0, 0}) == 1 &&     // avg 0.5 > 0.45
        call(f().get(), 2, rep(2, 0), {1, 0}, {0, 0}) == 0 &&     // avg 0.25
        call(f().get(), 3, rep(3, 0), {1, 1, 1}, {1, 1, 1}) == 1; // avg 1
    report(ok, "FreqLearner: avg lifetime rate > 0.45 -> war");
  }

  // 13. MarkovLearner: fallback until >=8 obs; then predict next count.
  {
    auto f = Registry::instance().get("MarkovLearner");
    const bool ok =
        call(f().get(), 5, rep(5, 0), rep(5, 1), rep(5, 1)) == 0 &&  // 4 obs < 8
        // 9 obs, opps all-war (count 2) -> predicted next 2 -> war
        call(f().get(), 10, rep(10, 0), rep(10, 1), rep(10, 1)) == 1 &&
        // 9 obs, opps all-peace (count 0) -> predicted next 0 -> peace
        call(f().get(), 10, rep(10, 0), rep(10, 0), rep(10, 0)) == 0;
    report(ok, "MarkovLearner: fallback then predicted-count>=1 -> war");
  }

  // 14. CycleFitter: period detection over the last 8 moves.
  {
    auto a = Registry::instance().get("CycleFitter")();
    const bool ok =
        // n<8: mirror last (TFT_any)
        call(a.get(), 1, {0}, {1}, {0}) == 1 &&
        call(a.get(), 1, {0}, {0}, {0}) == 0 &&
        // opp1 period-2 predicted-peace (m[6]=0) -> war
        call(a.get(), 8, rep(8, 0), {0, 1, 0, 1, 0, 1, 0, 1}, rep(8, 0)) == 1 &&
        // both period-2 predicted-war (m[6]=1) -> peace
        call(a.get(), 8, rep(8, 0), {1, 0, 1, 0, 1, 0, 1, 0},
             {1, 0, 1, 0, 1, 0, 1, 0}) == 0 &&
        // opp1 non-periodic -> mirror last (=1) -> war
        call(a.get(), 8, rep(8, 0), {0, 0, 0, 0, 0, 0, 0, 1}, rep(8, 0)) == 1;
    report(ok, "CycleFitter: period-2/3 detection + mirror fallback");
  }

  // 15. AntiExploit: never initiate; engage on opp war; reset on 3 calm rds.
  {
    auto a = Registry::instance().get("AntiExploit")();
    const std::vector<int> o1f = {0, 1, 0, 0, 0};
    const std::vector<int> o2f = rep(5, 0);
    const std::vector<int> myf = rep(5, 0);
    const int ns[5] = {0, 1, 2, 3, 5};
    const int expect[5] = {0, 0, 1, 1, 0};
    bool ok = true;
    for (int k = 0; k < 5; ++k) {
      const int n = ns[k];
      std::vector<int> m(myf.begin(), myf.begin() + n);
      std::vector<int> o1(o1f.begin(), o1f.begin() + n);
      std::vector<int> o2(o2f.begin(), o2f.begin() + n);
      if (a->selectAction(n, m, o1, o2) != expect[k]) ok = false;
    }
    report(ok, "AntiExploit: react-only, reset after 3 calm rds");
  }

  // 16. T4Tsmart: war iff BOTH opps warred last; opens peace.
  {
    auto a = Registry::instance().get("T4Tsmart")();
    const bool ok =
        call(a.get(), 0, {}, {}, {}) == 0 &&
        call(a.get(), 1, {0}, {0}, {0}) == 0 &&
        call(a.get(), 1, {0}, {1}, {1}) == 1 &&  // both warred -> war
        call(a.get(), 1, {0}, {1}, {0}) == 0 &&  // only one -> peace
        call(a.get(), 2, {0, 0}, {1, 1}, {1, 1}) == 1;
    report(ok, "T4Tsmart: war iff both opps warred last");
  }
}

void test_unit_stochastic() {
  std::printf("--- scripted-history unit tests (Tier-2, stochastic, seeded) ---\n");

  // 6. GenZD3P: p(peace) 0.9/0.5/0.1 by opp last-round war-count.
  {
    auto a = Registry::instance().get("GenZD3P")();
    // c=0 (both peace) -> p(peace)=0.9 -> war iff uniform >= 0.9
    const double u0 = predict_uniform(100001);
    mas_sim::fast_rng().reseed(100001);
    const int r0 = call(a.get(), 1, {0}, {0}, {0});
    report(r0 == (u0 >= 0.9 ? 1 : 0),
           "GenZD3P: c=0 -> p(peace)=0.9 (seed 100001)");

    // c=1 (one opp war) -> p(peace)=0.5
    const double u1 = predict_uniform(100002);
    mas_sim::fast_rng().reseed(100002);
    const int r1 = call(a.get(), 1, {0}, {1}, {0});
    report(r1 == (u1 >= 0.5 ? 1 : 0),
           "GenZD3P: c=1 -> p(peace)=0.5 (seed 100002)");

    // c=2 (both war) -> p(peace)=0.1
    const double u2 = predict_uniform(100003);
    mas_sim::fast_rng().reseed(100003);
    const int r2 = call(a.get(), 1, {0}, {1}, {1});
    report(r2 == (u2 >= 0.1 ? 1 : 0),
           "GenZD3P: c=2 -> p(peace)=0.1 (seed 100003)");
  }

  // 1. Prober_DDCC farming branch: exactly 1 opp war in the probe -> farm at
  //    rate 0.35 (war iff uniform < 0.35).
  {
    auto a = Registry::instance().get("Prober_DDCC")();
    const double u = predict_uniform(200001);
    mas_sim::fast_rng().reseed(200001);
    const int r = call(a.get(), 4, rep(4, 0), {0, 1, 0, 0}, rep(4, 0));
    report(r == (u < 0.35 ? 1 : 0),
           "Prober_DDCC: farming branch war iff uniform<0.35");
  }

  // 11. HybridB farming branch: no opp war in rds 0..8 -> farm at rate 0.50.
  {
    auto a = Registry::instance().get("HybridB")();
    const double u = predict_uniform(200002);
    mas_sim::fast_rng().reseed(200002);
    const int r = call(a.get(), 9, rep(9, 0), rep(9, 0), rep(9, 0));
    report(r == (u < 0.50 ? 1 : 0),
           "HybridB: farming branch war iff uniform<0.50");
  }

  // 16. T4Tsmart forgiveness flip: after an own retaliation (base war), flip
  //     to peace with p 0.05.
  {
    auto a = Registry::instance().get("T4Tsmart")();
    const double u = predict_uniform(200003);
    mas_sim::fast_rng().reseed(200003);
    const int r = call(a.get(), 1, {1}, {1}, {1});  // a=1, my=1 -> maybe flip
    report(r == (u < 0.05 ? 0 : 1),
           "T4Tsmart: 5% forgiveness flip after own retaliation");
  }

  // 5. Omega3P: feud detector triggers 2-rd amnesty (jitter consumes one
  //     draw; the returned action is 0 either way).
  {
    auto a = Registry::instance().get("Omega3P")();
    mas_sim::fast_rng().reseed(200004);
    // n=1: base war (opp1 warred), no feud check yet.
    const int r1 = call(a.get(), 1, {0}, {1}, {0});
    // n=6: last 6 have >=4 opp-war-rounds (o1 all war) and I warred >=4 ->
    //      feud -> amnesty (return 0).
    const int r6 = call(a.get(), 6, {0, 1, 1, 1, 1, 1}, rep(6, 1), rep(6, 0));
    // n=7: still in amnesty -> 0.
    const int r7 = call(a.get(), 7, {0, 1, 1, 1, 1, 1, 1}, rep(7, 1), rep(7, 0));
    report(r1 == 1 && r6 == 0 && r7 == 0,
           "Omega3P: feud (>=4/6 opp-war-rd + >=4 self-war) -> amnesty");
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
  std::printf("--- smoke tournament (16 Tier-2 + 4 Tier-0 anchors, 5 runs) ---\n");

  const std::vector<std::string> t2 = {
      "Prober_DDCC", "SaintHunter", "Gradual3P", "CTFT3P", "Omega3P",
      "GenZD3P",     "KLenient2",   "KLenient3", "EndgameGTFT",
      "HybridA",     "HybridB",     "FreqLearner", "MarkovLearner",
      "CycleFitter", "AntiExploit", "T4Tsmart"};
  const std::vector<std::string> anchors = {"NiceAgent", "NastyAgent",
                                            "RandomAgent", "T4TAgent"};

  // All 16 Tier-2 names present in this build.
  bool all_registered = true;
  for (const auto& n : t2)
    if (!Registry::instance().has(n)) all_registered = false;
  report(all_registered, "all 16 Tier-2 names registered");

  std::vector<mas_sim::AgentFactory> factories;
  factories.reserve(t2.size() + anchors.size());
  for (const auto& n : t2) factories.push_back(Registry::instance().get(n));
  for (const auto& n : anchors)
    factories.push_back(Registry::instance().get(n));
  const int N = static_cast<int>(factories.size());  // 20

  bool no_throw = true, all_finite = true;
  for (int r = 0; r < 5; ++r) {
    try {
      const auto s =
          run_tournament(factories, N, 5000 + r, TournamentMode::kDistinct,
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
        run_tournament(factories, N, 987654, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    const auto b =
        run_tournament(factories, N, 987654, TournamentMode::kDistinct,
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

  // (a) SaintHunter vs {Nice, Nice} >= 5.5/rd.
  //   rds 0..14 all-peace (4/rd); rds 15..R-1 war vs two Nices (6/rd).
  //   avg = 6 - 30/R for R in [90,110] => [5.667, 5.727] (>= 5.5).
  {
    const auto sh = Registry::instance().get("SaintHunter");
    const auto nice = Registry::instance().get("NiceAgent");
    double avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = sh;
      f[1] = nice;
      f[2] = nice;
      const auto s =
          run_tournament(f, 3, 40000 + r, TournamentMode::kDistinct,
                         RngKind::kSfc64);
      avg += s[0];
    }
    avg /= kRuns;
    std::printf("      SaintHunter vs Nice-pair = %.4f/rd\n", avg);
    report(avg >= 5.5, "SaintHunter vs Nice-pair >= 5.5/rd");
  }

  // (b) Gradual3P vs {Nasty, Nasty}. LITERAL spec says ">= 0/rd".
  //   Flag: vs two pure-Nasty players the per-round ceiling is 0
  //   (payoff[1][1][1]=0 when at war; payoff[0][1][1]=-1 when at peace), so
  //   any bot that ever plays peace is strictly < 0; exactly 0 is only
  //   reachable by a 100%-war bot. The 2-rd amnesty makes Gradual3P play
  //   some peace, so the literal ">= 0" is unsatisfiable (same situation as
  //   the B2 Nice>Nasty flag). We assert the TRUE, verifiable bounds instead
  //   and flag the literal direction.
  {
    const auto g = Registry::instance().get("Gradual3P");
    const auto nasty = Registry::instance().get("NastyAgent");
    double avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = g;
      f[1] = nasty;
      f[2] = nasty;
      const auto s =
          run_tournament(f, 3, 41000 + r, TournamentMode::kDistinct,
                         RngKind::kSfc64);
      avg += s[0];
    }
    avg /= kRuns;
    std::printf("      Gradual3P vs Nasty-pair = %.4f/rd "
                "(literal '>= 0' unsatisfiable; ceiling is 0)\n", avg);
    report(std::isfinite(avg) && avg <= 0.0 && avg > -0.5,
           "Gradual3P vs Nasty-pair: finite, <= 0, > -0.5 "
           "(literal '>= 0' FLAGGED as false)");
  }

  // (c) AntiExploit vs {Nasty, Nasty} >= -0.5/rd.
  //   r0 peace (-1), then permanent war (0) forever (Nastys never give a
  //   3-calm window) => avg = -1/R in [-0.011, -0.010].
  {
    const auto ae = Registry::instance().get("AntiExploit");
    const auto nasty = Registry::instance().get("NastyAgent");
    double avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = ae;
      f[1] = nasty;
      f[2] = nasty;
      const auto s =
          run_tournament(f, 3, 42000 + r, TournamentMode::kDistinct,
                         RngKind::kSfc64);
      avg += s[0];
    }
    avg /= kRuns;
    std::printf("      AntiExploit vs Nasty-pair = %.4f/rd\n", avg);
    report(avg >= -0.5, "AntiExploit vs Nasty-pair >= -0.5/rd");
  }
}

}  // namespace

int main() {
  std::printf("=== zoo_t2_test ===\n");
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
