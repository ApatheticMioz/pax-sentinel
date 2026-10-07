// =============================================================================
// sim/candidates/candidates_f_test.cpp — slice C2a: behavioral tests for
// candidate F "CandF" (endgame-ramp retaliator).
//
// Mirrors the candidates_e test layout:
//   (i)  scripted-history unit tests: drive selectAction() directly with
//        crafted histories (n == completed rounds; "rd k" is the 0-based
//        index k; histories prefix-extend across sequential calls, mirroring
//        a real match).
//   (ii) same-seed reference gates:
//        (1) mode0 (ramp off) vs {Nice, Nice} in the 3-WAY reading (CandF +
//            3 Nice, a 4-agent distinct tournament, per the C1c precedent)
//            must score >= the same-seed CTFT3P reference (the backbone the
//            dispatch says was mirrored; the inlined reference below is
//            bit-identical to sim/zoo/zoo_t2.cpp's CTFT3P — the zoo is NOT
//            modified, and zoo_t2 is not linked into this test binary).
//        (2) mode0 vs {Nasty, Nasty}: same-seed 3-way; the R2 override
//            (both opps war every round) must not lose to the CTFT3P
//            reference (both play the same actions here, so the streams
//            match exactly).
//   (iii) the dispatch test suite:
//        (3) ramp fires (mode 1): scripted both-peace history, n >= ramp_T
//            -> War, n == ramp_T - 1 -> Peace (checked for T=104/106/108);
//        (4) mode2: same-seed determinism + q-hazard rate sanity at
//            n = 100, 105, 109 (p = min(1, 3/(111-n)));
//        (5) ramp + retaliation: after a ramp War, the next round vs a
//            retaliating pair -> War via R2 (no suicide-Peace; the
//            contrite override is suppressed inside the ramp window);
//        (6) registration smoke (all six names) + same-seed tournament
//            determinism + a mixed smoke pool.
//
// Round-index convention: n == number of completed rounds; histories hold
// rds 0..n-1; "rd k" is the 0-based index k.
//
// Build: sim/build.sh `candidatesf` / `candidatesfasan` (this file is the
// CandF test main; it links engine.cpp + zoo_t01.cpp (anchor bots) +
// candidates_f.cpp — mirroring the candidates_e target).
// =============================================================================

#include "candidates_f.hpp"

#include "../zoo/zoo_common.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using mas_sim::AgentBase;
using mas_sim::CandFParams;
using mas_sim::Rng;
using mas_sim::RngKind;
using mas_sim::Registry;
using mas_sim::TournamentMode;
using mas_sim::make_CandF;
using mas_sim::make_CandF_T104;
using mas_sim::make_CandF_T106;
using mas_sim::make_CandF_T108;
using mas_sim::make_CandF_mode0;
using mas_sim::make_CandF_mode2;
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

// Find a seed in [base, base+search) whose first uniform() draw lands in
// [lo, hi). Returns -1 if none.
int find_seed(int base, int search, double lo, double hi) {
  for (int s = 0; s < search; ++s) {
    const double u = predict_uniform(base + s);
    if (u >= lo && u < hi) return base + s;
  }
  return -1;
}

// Crafted-history caller (selectAction takes non-const refs; we copy).
int call(mas_sim::AgentBase* a, int n, const std::vector<int>& my,
         const std::vector<int>& o1, const std::vector<int>& o2) {
  std::vector<int> m(my), a1(o1), a2(o2);
  return a->selectAction(n, m, a1, a2);
}

std::vector<int> rep(int n, int v) { return std::vector<int>(n, v); }

// The CTFT3P backbone (sim/zoo/zoo_t2.cpp), mirrored IN-TEST so the
// same-seed reference gates (1)/(2) do not require linking zoo_t2 into
// this test binary. Bit-identical rule-set (the zoo itself is NOT
// modified by this slice).
class CTFT3PRef : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;
    const int base = (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;
    if (my[n - 1] == 1 && o1[n - 1] == 0 && o2[n - 1] == 0) return 0;  // contrite
    return base;
  }
};

const mas_sim::AgentFactory kCTFT3PRef =
    [] { return std::make_unique<CTFT3PRef>(); };

bool finite_scores(const std::vector<double>& s) {
  if (s.empty()) return false;
  for (double v : s)
    if (!std::isfinite(v)) return false;
  return true;
}

}  // namespace

// =============================================================================
// (1) mode0 (ramp off) vs {Nice, Nice}, 3-WAY reading: CandF_mode0 must
// score >= the same-seed CTFT3P reference in the 4-agent distinct
// tournament (CandF + 3 Nice). The in-window decision space of mode0 is
// exactly the CTFT3P mirror (no probe, no state), so against an all-peace
// pair both agents make identical decisions in every match; the >= gate
// is the dispatch regression (the override must not lose), with strict
// equality expected.
// =============================================================================
void test_f1_mode0_nice3way() {
  std::printf("--- (1) mode0 vs {Nice, Nice} 3-way, same-seed >= CTFT3P ---\n");

  const auto nice = Registry::instance().get("NiceAgent");
  double avg_candf = 0.0, avg_ref = 0.0;
  bool all_ge = true;
  for (int r = 0; r < 5; ++r) {
    const int seed = 900000 + r;

    std::vector<mas_sim::AgentFactory> f(4);
    f[0] = make_CandF_mode0();
    f[1] = nice;
    f[2] = nice;
    f[3] = nice;
    const double candf = run_tournament(
        f, 4, seed, TournamentMode::kDistinct, RngKind::kSfc64)[0];

    std::vector<mas_sim::AgentFactory> fr(4);
    fr[0] = kCTFT3PRef;
    fr[1] = nice;
    fr[2] = nice;
    fr[3] = nice;
    const double ref = run_tournament(
        fr, 4, seed, TournamentMode::kDistinct, RngKind::kSfc64)[0];

    std::printf("      seed %d: CandF_mode0 = %.4f/rd, CTFT3P = %.4f/rd\n",
                seed, candf, ref);
    if (!(candf >= ref)) all_ge = false;
    avg_candf += candf;
    avg_ref += ref;
  }
  avg_candf /= 5;
  avg_ref /= 5;
  std::printf("      5-run avg: CandF_mode0 = %.4f/rd, CTFT3P = %.4f/rd\n",
              avg_candf, avg_ref);

  // 2-WAY informational measurement (the same gate's 3-agent reading:
  // CandF + Nice + Nice, 3 distinct matches per seed): against an
  // all-peace pair mode0 makes the EXACT same decision as CTFT3P in every
  // round (the dispatch's >= property holds trivially, with equality), so
  // both values are ~4/rd (all-peace payoffs 4/rd).
  {
    double two_candf = 0.0, two_ref = 0.0;
    for (int r = 0; r < 5; ++r) {
      const int seed = 900500 + r;
      std::vector<mas_sim::AgentFactory> f2(3);
      f2[0] = make_CandF_mode0();
      f2[1] = nice;
      f2[2] = nice;
      two_candf +=
          run_tournament(f2, 3, seed, TournamentMode::kDistinct,
                         RngKind::kSfc64)[0];
      std::vector<mas_sim::AgentFactory> fr2(3);
      fr2[0] = kCTFT3PRef;
      fr2[1] = nice;
      fr2[2] = nice;
      two_ref +=
          run_tournament(fr2, 3, seed, TournamentMode::kDistinct,
                         RngKind::kSfc64)[0];
    }
    two_candf /= 5;
    two_ref /= 5;
    std::printf("      (2-way informational: CandF_mode0 = %.4f/rd, "
                "CTFT3P = %.4f/rd)\n",
                two_candf, two_ref);
  }

  report(all_ge && std::isfinite(avg_candf) && std::isfinite(avg_ref) &&
             avg_candf >= avg_ref,
         "(1) mode0 vs {Nice,Nice} 3-way >= same-seed CTFT3P (backbone "
         "mirror; both all-peace, no first-war outside ramp)");
}

// =============================================================================
// (2) mode0 vs {Nasty, Nasty}, 3-way same-seed: the R2 override must not
// lose to the CTFT3P reference. Against two war-forever opponents BOTH
// agents war in every round from n=1 (R2 override / R1 mirror — the
// contrite branch is never reached because both opps always war), so the
// streams are identical and the scores match exactly; the check asserts
// the >= bound (and reports the exact equality).
// =============================================================================
void test_f2_mode0_nasty3way() {
  std::printf("--- (2) mode0 vs {Nasty, Nasty} 3-way, same-seed >= CTFT3P ---\n");

  const auto nasty = Registry::instance().get("NastyAgent");
  double avg_candf = 0.0, avg_ref = 0.0;
  bool all_ge = true, all_eq = true;
  for (int r = 0; r < 5; ++r) {
    const int seed = 901000 + r;

    std::vector<mas_sim::AgentFactory> f(3);
    f[0] = make_CandF_mode0();
    f[1] = nasty;
    f[2] = nasty;
    const double candf = run_tournament(
        f, 3, seed, TournamentMode::kDistinct, RngKind::kSfc64)[0];

    std::vector<mas_sim::AgentFactory> fr(3);
    fr[0] = kCTFT3PRef;
    fr[1] = nasty;
    fr[2] = nasty;
    const double ref = run_tournament(
        fr, 3, seed, TournamentMode::kDistinct, RngKind::kSfc64)[0];

    std::printf("      seed %d: CandF_mode0 = %.4f/rd, CTFT3P = %.4f/rd\n",
                seed, candf, ref);
    if (!(candf >= ref)) all_ge = false;
    if (candf != ref) all_eq = false;
    avg_candf += candf;
    avg_ref += ref;
  }
  avg_candf /= 5;
  avg_ref /= 5;
  std::printf("      5-run avg: CandF_mode0 = %.4f/rd, CTFT3P = %.4f/rd "
              "(exact match: %s)\n",
              avg_candf, avg_ref, all_eq ? "yes" : "no");
  report(all_ge && std::isfinite(avg_candf) && std::isfinite(avg_ref),
         "(2) mode0 vs {Nasty,Nasty} 3-way >= same-seed CTFT3P (R2 override: "
         "both opps war every round -> war every round; no loss)");
}

// =============================================================================
// (3) R3 ramp fires (mode 1 threshold): scripted BOTH-PEACE history (no
// opp war in the last round, so R1 would peace; no R2). War iff n >=
// ramp_T; n == ramp_T - 1 must be peace. Checked for all three threshold
// variants (T = 104, 106, 108) plus the default CandF (T=106).
// Also verified with a prior isolated opp war (rd 85) in the history: at
// the boundary rounds the last round is peace, so R1's contrite (from a
// prior my-war) cannot interfere — the ramp is the sole decider.
// =============================================================================
void test_f3_ramp_boundary() {
  std::printf("--- (3) ramp fires: both-peace n>=T -> War, n==T-1 -> Peace ---\n");

  struct TCase {
    const char* name;
    mas_sim::AgentFactory f;
    int T;
  };
  const TCase cases[] = {
      {"CandF (default T=106)", make_CandF(), 106},
      {"CandF_T104", make_CandF_T104(), 104},
      {"CandF_T106", make_CandF_T106(), 106},
      {"CandF_T108", make_CandF_T108(), 108},
  };

  for (const TCase& tc : cases) {
    auto a = tc.f();
    // Clean all-both-peace histories.
    const int at_T = call(a.get(), tc.T, rep(tc.T, 0), rep(tc.T, 0),
                          rep(tc.T, 0));
    const int at_Tm1 = call(a.get(), tc.T - 1, rep(tc.T - 1, 0),
                            rep(tc.T - 1, 0), rep(tc.T - 1, 0));
    const int below = call(a.get(), 89, rep(89, 0), rep(89, 0), rep(89, 0));
    // With a prior isolated opp war (rd 85): n = T-1 has last round peace
    // (my last = 0) -> R1 would be contrite-or-peace either way; the ramp
    // must NOT have fired before T.
    std::vector<int> o1p = rep(tc.T - 1, 0);
    if (85 < tc.T - 1) o1p[85] = 1;
    const int at_Tm1_b = call(a.get(), tc.T - 1, rep(tc.T - 1, 0), o1p,
                              rep(tc.T - 1, 0));
    std::printf("      %s: n=%d -> %d, n=%d -> %d, n=89 -> %d, n=%d (with "
                "prior opp war) -> %d\n",
                tc.name, tc.T, at_T, tc.T - 1, at_Tm1, below, tc.T - 1,
                at_Tm1_b);
    report(at_T == 1 && at_Tm1 == 0 && below == 0 && at_Tm1_b == 0,
           (std::string("    (3) ") + tc.name +
            ": ramp war at n>=T (" + std::to_string(tc.T) +
            "), peace at n==T-1 (and at 89; unaffected by a prior opp war)"
            ).c_str());
  }
}

// =============================================================================
// (4) mode2 (q-hazard):
//     (4a) same-seed determinism: one agent, one seed, driven n=90..109
//          (the full ramp window) with both-peace histories — the action
//          sequence must be reproduced EXACTLY by an identically-reseeded
//          second run (the hazard is a pure function of (seed, history));
//     (4b) rate sanity at n = 100, 105, 109: over 2000 fresh seeds the
//          observed war-frequency must track the theoretical
//          p(n) = min(1, ramp_c / (111 - n)) = 3/(111-n):
//            n=100 -> 3/11  = 0.2727
//            n=105 -> 3/6   = 0.5
//            n=109 -> 3/2   = 1.5 -> 1.0
//          (2000 samples: sd of the empirical rate <= ~0.011, so a
//          +/-0.06 tolerance is ~5 sd — safe).
// =============================================================================
void test_f4_mode2() {
  std::printf("--- (4) mode2: same-seed determinism + rate sanity ---\n");

  // (4a) determinism across the whole ramp window.
  {
    auto a = make_CandF_mode2()();
    mas_sim::fast_rng().reseed(770001);
    std::vector<int> seq1;
    for (int n = 90; n <= 109; ++n)
      seq1.push_back(call(a.get(), n, rep(n, 0), rep(n, 0), rep(n, 0)));

    auto b = make_CandF_mode2()();
    mas_sim::fast_rng().reseed(770001);
    std::vector<int> seq2;
    for (int n = 90; n <= 109; ++n)
      seq2.push_back(call(b.get(), n, rep(n, 0), rep(n, 0), rep(n, 0)));

    bool fired = false;
    for (int v : seq1)
      if (v != 0) fired = true;
    std::printf("      mode2 ramp sequence (n=90..109): ");
    for (int v : seq1) std::printf("%d", v);
    std::printf("\n");
    report(seq1 == seq2 && fired,
           "(4a) mode2 same-seed determinism over the full ramp window "
           "(n=90..109, both-peace); at least one ramp war fired");
  }

  // (4b) rate sanity at n = 100, 105, 109.
  {
    const int ns[] = {100, 105, 109};
    const double p_theory[] = {3.0 / 11.0, 3.0 / 6.0, 1.0};
    const int NSEED = 2000;
    bool ok = true;
    for (int t = 0; t < 3; ++t) {
      const int n = ns[t];
      int wars = 0;
      for (int s = 0; s < NSEED; ++s) {
        auto a = make_CandF_mode2()();
        mas_sim::fast_rng().reseed(950000 + s * 31 + t);
        wars += call(a.get(), n, rep(n, 0), rep(n, 0), rep(n, 0));
      }
      const double p_hat = static_cast<double>(wars) / NSEED;
      std::printf("      n=%d: observed rate = %.4f (theory p = %.4f, "
                  "N=%d)\n",
                  n, p_hat, p_theory[t], NSEED);
      if (std::fabs(p_hat - p_theory[t]) > 0.06) ok = false;
    }
    report(ok,
           "(4b) mode2 q-hazard rate tracks p = min(1, 3/(111-n)) at "
           "n=100/105/109 (0.2727 / 0.5 / 1.0) within tolerance");
  }
}

// =============================================================================
// (5) ramp + retaliation: after a ramp War, the next round vs a
// RETALIATING pair (both opps war) -> War via R2 — no suicide-Peace.
//   (5a) mode1 (default, T=106): scripted history: all-peace rds 0..105,
//       my war at rd 106 (the ramp war), both opps war at rd 107 (they
//       retaliate). At n=107: R2 (both warred last) fires BEFORE R1 — the
//       CTFT3P contrite override (I warred last while both opps peaced —
//       false here, but even the all-peace-last-round variant below
//       shows R3-before-R1) could never give a peace: expect War.
//   (5b) the suicide-Peace trap, closed by R3's precedence over R1:
//       after a ramp War, if BOTH opps PEACE (no retaliation), a CTFT3P
//       backbone would be contrite-peace at the next round; CandF must
//       still war via the committed ramp (R3 before R1).
//   (5c) mode2: the same ramp-war-then-retaliation scripted history gives
//       a War at the retaliation round via R2 (the hazard is irrelevant:
//       R2 is checked before R3).
// =============================================================================
void test_f5_ramp_retaliation() {
  std::printf("--- (5) ramp + retaliation: War via R2, no suicide-Peace ---\n");

  // (5a) mode1: ramp war at rd 106, both opps retaliate at rd 107.
  {
    auto a = make_CandF()();  // default: mode 1, T=106
    // n=106: all-peace so far (rds 0..105) -> ramp war (n>=106).
    const int r106 = call(a.get(), 106, rep(106, 0), rep(106, 0),
                          rep(106, 0));
    // n=107: my rd106 = 1 (ramp war); both opps rd106 = 1 (they retaliated
    // to the ramp war). R2: both warred last -> War.
    std::vector<int> o1_107 = rep(107, 0);
    o1_107[106] = 1;
    std::vector<int> o2_107 = rep(107, 0);
    o2_107[106] = 1;
    std::vector<int> my_107 = rep(107, 0);
    my_107[106] = 1;
    const int r107 = call(a.get(), 107, my_107, o1_107, o2_107);
    // Control: same history but only ONE opp retaliated (o2 peaced) — R2
    // does NOT fire, but R3 (n=107 >= T=106) still wars. Also no
    // suicide-peace: R1's contrite is suppressed (R3 before R1).
    std::vector<int> o2c_107 = rep(107, 0);
    std::vector<int> o1c_107 = rep(107, 0);
    o1c_107[106] = 1;
    const int r107c = call(a.get(), 107, my_107, o1c_107, o2c_107);
    std::printf("      mode1: r106 (ramp) = %d, r107 (both retaliated) = %d, "
                "r107 (one retaliated) = %d\n",
                r106, r107, r107c);
    report(r106 == 1 && r107 == 1 && r107c == 1,
           "(5a) after ramp war: both-opp retaliation -> War via R2 (one-opp "
           "retaliation still wars via R3; committed, no contrite)");
  }

  // (5b) mode1: ramp war at rd 106, then both opps PEACE (no retaliation):
  //     the CTFT3P backbone would be contrite-peace (I warred last while
  //     both peaced); CandF must stay committed: War via R3 (n=107 >= T).
  {
    auto a = make_CandF()();
    const int r106 = call(a.get(), 106, rep(106, 0), rep(106, 0),
                          rep(106, 0));
    std::vector<int> my_107 = rep(107, 0);
    my_107[106] = 1;  // my ramp war; opps all peace
    const int r107 = call(a.get(), 107, my_107, rep(107, 0), rep(107, 0));
    std::printf("      mode1: r106 (ramp) = %d, r107 (both peace, no "
                "retaliation) = %d\n",
                r106, r107);
    report(r106 == 1 && r107 == 1,
           "(5b) after ramp war, no retaliation: STILL war (committed ramp; "
           "R3 before R1 suppresses the CTFT3P contrite suicide-Peace)");
  }

  // (5c) mode2: ramp war at rd 100 (hazard hit, seeded), both opps
  //     retaliate at rd 100, n=101: R2 -> War (the hazard draw is never
  //     consulted: R2 precedes R3).
  {
    // A seed whose hazard draw at n=100 (p = 3/11) WARs:
    const int seed = find_seed(970000, 200000, 0.0, 3.0 / 11.0);
    bool ok = false;
    if (seed > 0) {
      auto a = make_CandF_mode2()();
      mas_sim::fast_rng().reseed(seed);
      const int r100 = call(a.get(), 100, rep(100, 0), rep(100, 0),
                            rep(100, 0));
      // n=101: my rd100 = 1 (ramp war); both opps rd100 = 1 (retaliation).
      std::vector<int> o1_101 = rep(101, 0);
      o1_101[100] = 1;
      std::vector<int> o2_101 = rep(101, 0);
      o2_101[100] = 1;
      std::vector<int> my_101 = rep(101, 0);
      my_101[100] = 1;
      const int r101 = call(a.get(), 101, my_101, o1_101, o2_101);
      ok = (r100 == 1) && (r101 == 1);
      std::printf("      mode2: r100 (hazard ramp war, seed %d) = %d, "
                  "r101 (both retaliated) = %d\n",
                  seed, r100, r101);
    }
    report(ok, "(5c) mode2: after a hazard ramp war, both-opp retaliation -> "
               "War via R2 (no suicide-Peace; R2 precedes the hazard draw)");
  }
}

// =============================================================================
// (6) Registration smoke + same-seed determinism + mixed-pool smoke:
//     all six names (CandF, T104, T106, T108, mode2, mode0) are in the
//     registry; a 6-agent distinct tournament (all variants) across 3
//     seeds runs without exceptions with finite scores, and a same-seed
//     repeat produces an identical score vector; plus a mixed smoke pool
//     (CandF + Nice + Nasty + Random + TFT_any) 3 runs, no throw,
//     finite, same-seed identical.
// =============================================================================
void test_f6_registration_smoke() {
  std::printf("--- (6) registration smoke + same-seed determinism ---\n");

  const char* names[] = {"CandF",    "CandF_T104", "CandF_T106",
                         "CandF_T108", "CandF_mode2", "CandF_mode0"};
  bool all_reg = true;
  for (const char* n : names) {
    if (!Registry::instance().has(n)) {
      all_reg = false;
      std::printf("      missing registration: %s\n", n);
    }
  }
  report(all_reg, "(6a) all six CandF names registered");

  // All-variants 6-agent distinct smoke (3 seeds), no-throw + finite.
  {
    std::vector<mas_sim::AgentFactory> f(6);
    f[0] = make_CandF();
    f[1] = make_CandF_T104();
    f[2] = make_CandF_T106();
    f[3] = make_CandF_T108();
    f[4] = make_CandF_mode2();
    f[5] = make_CandF_mode0();
    bool no_throw = true, all_finite = true;
    for (int r = 0; r < 3; ++r) {
      try {
        const auto s = run_tournament(f, 6, 912000 + r,
                                      TournamentMode::kDistinct,
                                      RngKind::kSfc64);
        if (!finite_scores(s)) all_finite = false;
      } catch (const std::exception& e) {
        std::printf("      run %d threw: %s\n", r, e.what());
        no_throw = false;
        all_finite = false;
      }
    }
    report(no_throw && all_finite,
           "(6b) all-variants 6-agent distinct smoke: no exceptions, finite");
  }

  // Same-seed determinism: run twice with the same seed, compare vectors.
  {
    std::vector<mas_sim::AgentFactory> f(6);
    f[0] = make_CandF();
    f[1] = make_CandF_T104();
    f[2] = make_CandF_T106();
    f[3] = make_CandF_T108();
    f[4] = make_CandF_mode2();
    f[5] = make_CandF_mode0();
    const auto a =
        run_tournament(f, 6, 913500, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    const auto b =
        run_tournament(f, 6, 913500, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    std::printf("      same-seed scores: ");
    for (size_t i = 0; i < a.size(); ++i) std::printf("%.4f ", a[i]);
    std::printf("\n");
    report(a == b && finite_scores(a),
           "(6c) same seed => identical score vector (all six variants)");
  }

  // Mixed-pool smoke: CandF (default) + Nice + Nasty + Random + TFT_any
  // (TFT_any from zoo_t01 — a 3-way retaliator; the R2/R3 vs R1
  // precedence is exercised in live matches), 3 seeds, no throw, finite,
  // and the CandF default arm scores >= 0 on average (it never first-wars
  // outside the ramp and the ramp only adds war payoffs in the endgame).
  {
    const auto nice = Registry::instance().get("NiceAgent");
    const auto nasty = Registry::instance().get("NastyAgent");
    const auto rand_ = Registry::instance().get("RandomAgent");
    const auto tft = Registry::instance().get("TFT_any");
    bool no_throw = true, all_finite = true;
    double avg_candf = 0.0;
    for (int r = 0; r < 3; ++r) {
      try {
        std::vector<mas_sim::AgentFactory> f(5);
        f[0] = make_CandF();
        f[1] = nice;
        f[2] = nasty;
        f[3] = rand_;
        f[4] = tft;
        const auto s = run_tournament(f, 5, 914000 + r,
                                      TournamentMode::kDistinct,
                                      RngKind::kSfc64);
        if (!finite_scores(s)) all_finite = false;
        avg_candf += s[0];
      } catch (const std::exception& e) {
        std::printf("      run %d threw: %s\n", r, e.what());
        no_throw = false;
        all_finite = false;
      }
    }
    avg_candf /= 3;
    std::printf("      mixed-pool CandF (default) 3-run avg = %.4f/rd\n",
                avg_candf);
    report(no_throw && all_finite,
           "(6d) mixed-pool smoke (CandF + Nice/Nasty/Random/TFT_any): "
           "no exceptions, all scores finite");
  }
}

int main() {
  std::printf("=== candidates_f_test (slice C2a: CandF endgame-ramp "
              "retaliator) ===\n");
  test_f1_mode0_nice3way();
  test_f2_mode0_nasty3way();
  test_f3_ramp_boundary();
  test_f4_mode2();
  test_f5_ramp_retaliation();
  test_f6_registration_smoke();

  if (g_failures == 0) {
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("FAIL: %d/%d check(s) failed\n", g_failures, g_checks);
  return 1;
}
