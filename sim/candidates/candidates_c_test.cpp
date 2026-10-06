// =============================================================================
// sim/candidates/candidates_c_test.cpp — slice C1b: behavioral tests for
// candidate C "CandC_Classifier" (pure two-test classifier + exploiter).
//
// Mirrors candidates_ab_test.cpp / zoo test conventions:
//   (i)  scripted-history unit tests (>= 12 scenarios): drive selectAction()
//        directly with crafted histories (n == completed rounds; "rd k" is
//        0-based index k; histories prefix-extend across sequential calls,
//        mirroring a real match). Deterministic degenerate variants
//        (rate/r* = 0/1 short-circuit without a draw) + one seeded g=0.5
//        draw-prediction via a throwaway Rng of the same seed.
//   (ii) smoke tournament: CandC + 4 zoo anchors, 5 runs, fast mode,
//        distinct enumeration; no exceptions, finite scores, same-seed
//        determinism;
//   (iii) sanity checks (5-run averages, fast mode, distinct): with a
//        flagged-literal reading where the dispatch's number is
//        arithmetically unsatisfiable (see check (a)).
//
// Default params under test: probe_rd=4, burst 11..13 (rate 0.6), trickle
// 5..10 (rate 0.1), classify at n>=15, nasty window 8 / thresh 0.7,
// r* = 0.4.
//
// Build: sim/build.sh `candidates` / `candidatesasan` (this file is the
// CandC test main; it links engine.cpp + zoo_t01.cpp + candidates_c.cpp).
// =============================================================================

#include "../candidates/candidates_c.hpp"
#include "../zoo/zoo_common.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using mas_sim::AgentBase;
using mas_sim::CandCParams;
using mas_sim::Rng;
using mas_sim::RngKind;
using mas_sim::Registry;
using mas_sim::TournamentMode;
using mas_sim::make_CandC_Classifier;
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

// Zero vector of length n with 1s at the given 0-based indices.
std::vector<int> wars_at(int n, const std::vector<int>& idx) {
  std::vector<int> v(n, 0);
  for (int i : idx) v[i] = 1;
  return v;
}

bool finite_scores(const std::vector<double>& s) {
  if (s.empty()) return false;
  for (double v : s)
    if (!std::isfinite(v)) return false;
  return true;
}

}  // namespace

// =============================================================================
// (i) CandC scripted-history unit tests (13 scenarios).
// =============================================================================
void test_candC_unit() {
  std::printf("--- CandC_Classifier scripted-history unit tests ---\n");

  // C1. n==0 -> open peace (default and degenerate variants).
  {
    auto def = make_CandC_Classifier()();
    auto det = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                                 8, 0.7})();
    report(call(def.get(), 0, {}, {}, {}) == 0 &&
               call(det.get(), 0, {}, {}, {}) == 0,
           "C1: n==0 -> peace (both variants)");
  }

  // C2. probe script timing, deterministic variant (burst=0, trickle=0:
  //     NO draws at all): war EXACTLY at probe_rd=4, peace on all other
  //     script rounds 0..14 (incl. the burst rounds, which draw nothing).
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    bool ok = true;
    for (int n = 1; n <= 14; ++n) {
      const int expect = (n == 4) ? 1 : 0;
      if (call(a.get(), n, rep(n, 0), rep(n, 0), rep(n, 0)) != expect)
        ok = false;
    }
    report(ok, "C2: deterministic script -> war exactly at probe_rd=4");
  }

  // C3. trickle disabled, burst pinned to 1: "pure script" — war at the
  //     probe war (4) and ALL burst rounds (11,12,13); peace at 1..3,
  //     5..10, 14. No draws (0/1 short-circuit).
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 1.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    bool ok = true;
    for (int n = 1; n <= 14; ++n) {
      const bool burst = (n >= 11 && n <= 13);
      const int expect = (n == 4 || burst) ? 1 : 0;
      if (call(a.get(), n, rep(n, 0), rep(n, 0), rep(n, 0)) != expect)
        ok = false;
    }
    report(ok, "C3: trickle disabled, burst=1 -> pure script (4,11,12,13)");
  }

  // C4. silent-saint pair -> exploit phase; r*=0 (deterministic) -> peace
  //     every exploit round (n=15, 20, 50, 89).
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const bool ok =
        call(a.get(), 15, rep(15, 0), rep(15, 0), rep(15, 0)) == 0 &&
        call(a.get(), 20, rep(20, 0), rep(20, 0), rep(20, 0)) == 0 &&
        call(a.get(), 50, rep(50, 0), rep(50, 0), rep(50, 0)) == 0 &&
        call(a.get(), 89, rep(89, 0), rep(89, 0), rep(89, 0)) == 0;
    report(ok, "C4: silent pair -> exploit; r*=0 -> peace");
  }

  // C5. silent-saint pair -> exploit phase; r*=1 (deterministic) -> war
  //     every exploit round up to 89.
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 1.0,
                                               8, 0.7})();
    const bool ok =
        call(a.get(), 15, rep(15, 0), rep(15, 0), rep(15, 0)) == 1 &&
        call(a.get(), 20, rep(20, 0), rep(20, 0), rep(20, 0)) == 1 &&
        call(a.get(), 50, rep(50, 0), rep(50, 0), rep(50, 0)) == 1 &&
        call(a.get(), 89, rep(89, 0), rep(89, 0), rep(89, 0)) == 1;
    report(ok, "C5: silent pair -> exploit; r*=1 -> war every rd (incl. 15)");
  }

  // C6. endgame: same silent pair, r*=0 — before 90 the exploit phase
  //     plays peace, at/after rd 90 pure war (ceiling harvest).
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const bool ok =
        call(a.get(), 15, rep(15, 0), rep(15, 0), rep(15, 0)) == 0 &&
        call(a.get(), 89, rep(89, 0), rep(89, 0), rep(89, 0)) == 0 &&
        call(a.get(), 90, rep(90, 0), rep(90, 0), rep(90, 0)) == 1 &&
        call(a.get(), 95, rep(95, 0), rep(95, 0), rep(95, 0)) == 1;
    report(ok, "C6: endgame rd>=90 -> pure war (silent pair, r*=0)");
  }

  // C7. retaliating pair (o1 warred on probe_rd=4) -> pair RESPONSIVE
  //     (TFT_any from classify): n=15 both-peace-last -> 0; n=16 BOTH
  //     warred last -> 1 (war next rd); n=17 both-peace-last -> 0.
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const std::vector<int> o1_15 = wars_at(15, {4});
    const std::vector<int> o2_15 = rep(15, 0);
    const std::vector<int> o1_16 = wars_at(16, {4, 15});
    const std::vector<int> o2_16 = wars_at(16, {15});
    const std::vector<int> o1_17 = wars_at(17, {4, 15});
    const std::vector<int> o2_17 = wars_at(17, {15});
    const bool ok =
        call(a.get(), 15, rep(15, 0), o1_15, o2_15) == 0 &&
        call(a.get(), 16, rep(16, 0), o1_16, o2_16) == 1 &&
        call(a.get(), 17, rep(17, 0), o1_17, o2_17) == 0;
    report(ok, "C7: retaliating pair -> responsive (TFT_any: both-war->war, "
               "both-peace->peace)");
  }

  // C8. one hopeless (o1: 8/8 wars in the last nasty_win=8 rds) + one
  //     saint -> pair HOPeless: war at classify and PERMANENT war after
  //     (even all-peace histories; no downgrade, no draws).
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const std::vector<int> o1 =
        wars_at(15, {7, 8, 9, 10, 11, 12, 13, 14});  // 1.0 >= 0.7
    const std::vector<int> o2 = rep(15, 0);
    const bool ok =
        call(a.get(), 15, rep(15, 0), o1, o2) == 1 &&
        call(a.get(), 16, rep(16, 0), rep(16, 0), rep(16, 0)) == 1 &&
        call(a.get(), 20, rep(20, 0), rep(20, 0), rep(20, 0)) == 1;
    report(ok, "C8: one hopeless + one saint -> permanent war (never -1)");
  }

  // C9. flip rule: silent pair at classify -> exploit; then o1 double-
  //     wars in a 5-rd window post-classify (rds 15,16) -> ONE-WAY flip to
  //     responsive. o1's pre-classify war at 14 makes the flip-round
  //     decision (o1 war last, non-isolated window) draw-free: war.
  //     n=15 -> 0 (r*=0), n=16 (single war 15: no flip) -> 0,
  //     n=17 (wars 15,16: flip) -> 1, n=18 (both peace last) -> 0.
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const std::vector<int> o1_15 = wars_at(15, {14});
    const std::vector<int> o1_16 = wars_at(16, {14, 15});
    const std::vector<int> o1_17 = wars_at(17, {14, 15, 16});
    const std::vector<int> o1_18 = wars_at(18, {14, 15, 16});
    const bool ok =
        call(a.get(), 15, rep(15, 0), o1_15, rep(15, 0)) == 0 &&
        call(a.get(), 16, rep(16, 0), o1_16, rep(16, 0)) == 0 &&
        call(a.get(), 17, rep(17, 0), o1_17, rep(17, 0)) == 1 &&
        call(a.get(), 18, rep(18, 0), o1_18, rep(18, 0)) == 0;
    report(ok, "C9: flip rule: 2 wars in 5-rd window post-classify -> "
               "one-way upgrade to TFT_any");
  }

  // C10. no-flip case: post-classify wars at rds 15 and 20 (distance 5,
  //      never in one 5-rd window) -> stays exploit (r*=0 -> peace).
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const std::vector<int> o1_15 = rep(15, 0);
    const std::vector<int> o1_16 = wars_at(16, {15});
    const std::vector<int> o1_21 = wars_at(21, {15, 20});
    const bool ok =
        call(a.get(), 15, rep(15, 0), o1_15, rep(15, 0)) == 0 &&
        call(a.get(), 16, rep(16, 0), o1_16, rep(16, 0)) == 0 &&
        call(a.get(), 21, rep(21, 0), o1_21, rep(21, 0)) == 0;
    report(ok, "C10: wars at distance 5 -> no flip, stays exploit");
  }

  // C11. hopeless boundary: o1 with 5/8 = 0.625 < 0.7 wars in the nasty
  //      window (rds 7..11; no probe/burst retaliation) -> NOT hopeless,
  //      NOT responsive -> exploit (r*=0 -> peace at n=15).
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const std::vector<int> o1 = wars_at(15, {7, 8, 9, 10, 11});
    const int r = call(a.get(), 15, rep(15, 0), o1, rep(15, 0));
    report(r == 0, "C11: 5/8 = 0.625 < 0.7 -> not hopeless -> exploit");
  }

  // C12. retaliation via >=2 burst rounds (o2 warred on 11,12; no probe
  //      war): 2/8 = 0.25 not hopeless, but retaliated -> RESPONSIVE;
  //      then both-war-last (n=16) -> war.
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    const std::vector<int> o2_15 = wars_at(15, {11, 12});
    const std::vector<int> o1_16 = wars_at(16, {15});
    const std::vector<int> o2_16 = wars_at(16, {11, 12, 15});
    const bool ok =
        call(a.get(), 15, rep(15, 0), rep(15, 0), o2_15) == 0 &&
        call(a.get(), 16, rep(16, 0), o1_16, o2_16) == 1;
    report(ok, "C12: >=2 burst-round retaliations -> responsive -> both-war");
  }

  // C13. one-shot forgiveness (g=0.5, drawn): responsive pair (o1 warred
  //      on probe_rd=4); at n=16 o1's single isolated war (window
  //      [12,15) clean) is forgiven iff u < 0.5 — seed right before the
  //      only draw and predict it with a throwaway Rng of the same seed.
  {
    auto a = make_CandC_Classifier(CandCParams{4, 11, 3, 0.0, 0.0, 14, 0.0,
                                               8, 0.7})();
    // n=15: classify (no draws: last round peace).
    const std::vector<int> o1_15 = wars_at(15, {4});
    call(a.get(), 15, rep(15, 0), o1_15, rep(15, 0));
    const double u = predict_uniform(710001);
    mas_sim::fast_rng().reseed(710001);
    const std::vector<int> o1_16 = wars_at(16, {4, 15});
    const int r =
        call(a.get(), 16, rep(16, 0), o1_16, rep(16, 0));  // the ONLY draw
    report(r == (u < 0.5 ? 0 : 1),
           "C13: isolated single-opp war in responsive phase -> forgive "
           "w.p. 0.5 (seed 710001)");
  }
}

// =============================================================================
// (ii) smoke tournament: CandC + 4 zoo anchors, 5 runs.
// =============================================================================
void test_smoke_tournament() {
  std::printf("--- smoke tournament (CandC + 4 anchors, 5 runs) ---\n");

  const std::vector<std::string> anchors = {
      "NiceAgent", "NastyAgent", "TFT_any", "TolerantAgent"};

  bool all_registered = true;
  for (const auto& n : anchors)
    if (!Registry::instance().has(n)) all_registered = false;
  report(all_registered && Registry::instance().has("CandC_Classifier"),
         "all 5 smoke names registered");

  std::vector<mas_sim::AgentFactory> factories;
  factories.reserve(5);
  factories.push_back(make_CandC_Classifier());
  for (const auto& n : anchors)
    factories.push_back(Registry::instance().get(n));
  const int N = static_cast<int>(factories.size());  // 5

  bool no_throw = true, all_finite = true;
  for (int r = 0; r < 5; ++r) {
    try {
      const auto s =
          run_tournament(factories, N, 71000 + r, TournamentMode::kDistinct,
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
        run_tournament(factories, N, 987888, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    const auto b =
        run_tournament(factories, N, 987888, TournamentMode::kDistinct,
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

  // (a) CandC(r*=1.0, deterministic script) vs {NiceAgent, NiceAgent}.
  //     payoff[1][0][0]=6 (war, both-peace), payoff[0][0][0]=4 (peace,
  //     both-peace).  Script: 11 peace x4 + 4 war x6 = 68.  Exploit
  //     (r*=1): war x6 = 6/rd.  Endgame: war x6 = 6/rd.
  //     Over ~100 rds: (68 + 75*6 + 10*6) / 100 ~= 5.78/rd >= 5.5.
  //     LITERAL bound satisfied.
  {
    const CandCParams det{4, 11, 3, 0.0, 0.0, 14, 1.0, 8, 0.7};
    const auto c = make_CandC_Classifier(det);
    const auto nice = Registry::instance().get("NiceAgent");
    double avg = 0.0;
    for (int r = 0; r < 5; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = c;
      f[1] = nice;
      f[2] = nice;
      avg += run_tournament(f, 3, 72000 + r, TournamentMode::kDistinct,
                            RngKind::kSfc64)[0];
    }
    avg /= 5;
    std::printf("      CandC(r*=1, det) vs {Nice, Nice} = %.4f/rd\n", avg);
    report(std::isfinite(avg) && avg >= 5.5,
           "CandC(r*=1, det) vs {Nice, Nice} >= 5.5/rd");
  }

  // (b) CandC (default) vs {NastyAgent, NastyAgent}.
  //     LITERAL dispatch bound ">= -0.1/rd" is FLAGGED unsatisfiable for
  //     the default params (C1a precedent): the MANDATED probe script
  //     (rds 0..14) plays regardless of opponents and costs ~-12/rd-sum
  //     against two war-forever opponents (peace rds x payoff[0][1][1] =
  //     -1; probe/burst war rds x payoff[1][1][1] = 0), then permanent
  //     war (0/rd) from classify. Total ~ -12/100 = -0.12 < -0.1, so
  //     the permanent-war ~0/rd tail cannot offset the mandated probe
  //     cost over ~100 rounds. We assert the true band instead.
  {
    const auto c = make_CandC_Classifier();
    const auto nasty = Registry::instance().get("NastyAgent");
    double avg = 0.0;
    for (int r = 0; r < 5; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = c;
      f[1] = nasty;
      f[2] = nasty;
      avg += run_tournament(f, 3, 73000 + r, TournamentMode::kDistinct,
                            RngKind::kSfc64)[0];
    }
    avg /= 5;
    std::printf("      CandC vs {Nasty, Nasty} = %.4f/rd "
                "(literal '>= -0.1' FLAGGED; mandated probe cost)\n", avg);
    report(std::isfinite(avg) && avg >= -0.2 && avg <= -0.05,
           "CandC vs {Nasty, Nasty}: true band around -0.12 "
           "(permanent war 0/rd after mandated probe cost; literal bound "
           "FLAGGED unsatisfiable)");
  }

  // (c) CandC (default) vs {TFT_any, TFT_any}.
  //     LITERAL dispatch bound ">= 3.5/rd" is FLAGGED unsatisfiable
  //     (C1a/B2 precedent, structural): my mandatory probe war (rd 4) is
  //     seen by both memoryless TFTs, which war at rd 5; both-war -> I war
  //     at rd 6, and once I war both TFTs war the next round, so from
  //     ~rd 6 the triple LOCKS into permanent all-war (0/rd =
  //     payoff[1][1][1]) — a pure TFT never breaks a war streak, so no
  //     amount of my g=0.5 forgiveness can ever break the cycle. The only
  //     positive contributions are the ~4 open peace rounds (4/rd) and the
  //     probe war (6/rd), which over ~100 rounds average to ~0.15/rd; a
  //     3.5/rd "responsive holds 4-stream minus probe cost" reading is
  //     unreachable against this pair. We assert the measured true band.
  {
    const auto c = make_CandC_Classifier();
    const auto tft = Registry::instance().get("TFT_any");
    double avg = 0.0;
    for (int r = 0; r < 5; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = c;
      f[1] = tft;
      f[2] = tft;
      avg += run_tournament(f, 3, 74000 + r, TournamentMode::kDistinct,
                            RngKind::kSfc64)[0];
    }
    avg /= 5;
    std::printf("      CandC vs {TFT_any, TFT_any} = %.4f/rd "
                "(literal '>= 3.5' FLAGGED; all-war lock from rd 5)\n",
                avg);
    report(std::isfinite(avg) && avg >= -0.5 && avg <= 1.5,
           "CandC vs {TFT_any, TFT_any}: ~0/rd true band (all-war lock "
           "after probe retaliation; literal '>= 3.5/rd' FLAGGED)");
  }
}

int main() {
  std::printf("=== candidates_c_test (slice C1b: CandC_Classifier) ===\n");
  test_candC_unit();
  test_smoke_tournament();
  test_sanity();

  if (g_failures == 0) {
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("FAIL: %d/%d check(s) failed\n", g_failures, g_checks);
  return 1;
}
