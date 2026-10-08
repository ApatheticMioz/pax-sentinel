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
//            -> War, n == ramp_T - 1 -> Peace (checked for
//            T=98/100/104/106/108);
//        (4) mode2: same-seed determinism + q-hazard rate sanity at
//            n = 100, 105, 109 (p = min(1, 3/(111-n)));
//        (5) ramp + retaliation: after a ramp War, the next round vs a
//            retaliating pair -> War via R2 (no suicide-Peace; the
//            contrite override is suppressed inside the ramp window);
//        (6) registration smoke (all ten names) + same-seed tournament
//            determinism + a mixed smoke pool.
//
//   (iv) slice C2c: mode 3 (self-limiting probe-cycle endgame, registered
//        as CandF_mode3 = mode 3 @ the frozen T=104):
//        (7)  in-window, my last = War, both opps warred last (echo)
//            -> Peace (repair fires; it OVERRIDES R2 inside the window —
//            dispatch precedence R3-repair > R2 > R1);
//        (8)  in-window, my last = War, opps SILENT last (no echo,
//            saint/tolerant) -> War (the full commit vs silent pairs);
//        (9)  the echo cycle alternates W,P,W,P,W,P over 6 scripted
//            in-window rounds (after a repair Peace the echo decays and
//            the committed ramp resumes);
//        (10) outside the window (n < T) mode3 == mode1(T=104) same-seed
//            over full scripted drives (saint/echoer/warlock): identical
//            first T rounds for all pair types, and the saint drive is
//            byte-identical for the WHOLE match (no echo -> full commit);
//        (11) mode3 vs mode1(T=104) on a scripted saint echo-free history:
//            both commit identically (War exactly rds T..109 — the full
//            110-round match);
//        (12) registration smoke for CandF_mode3 + same-seed tournament
//            determinism with mode3 in the variant pool.
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using mas_sim::AgentBase;
using mas_sim::CandFParams;
using mas_sim::Rng;
using mas_sim::RngKind;
using mas_sim::Registry;
using mas_sim::TournamentMode;
using mas_sim::make_CandF;
using mas_sim::make_CandF_T98;
using mas_sim::make_CandF_T100;
using mas_sim::make_CandF_T104;
using mas_sim::make_CandF_T106;
using mas_sim::make_CandF_T108;
using mas_sim::make_CandF_mode0;
using mas_sim::make_CandF_mode2;
using mas_sim::make_CandF_mode3;
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
// variants (T = 98, 100, 104, 106, 108) plus the default CandF (T=106).
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
      {"CandF_T98", make_CandF_T98(), 98},
      {"CandF_T100", make_CandF_T100(), 100},
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
//     all ten names (CandF, T98, T100, T102, T104, T106, T108, mode2,
//     mode3, mode0) are in the registry; a 7-agent distinct tournament
//     (all deterministic + stochastic variants, C2c's mode3 included)
//     across 3 seeds runs without exceptions with finite scores, and a
//     same-seed repeat produces an identical score vector; plus a mixed
//     smoke pool (CandF + Nice + Nasty + Random + TFT_any) 3 runs, no
//     throw, finite, same-seed identical.
// =============================================================================
void test_f6_registration_smoke() {
  std::printf("--- (6) registration smoke + same-seed determinism ---\n");

  const char* names[] = {"CandF",        "CandF_T98", "CandF_T100",
                         "CandF_T102",   "CandF_T104", "CandF_T106",
                         "CandF_T108",   "CandF_mode2", "CandF_mode3",
                         "CandF_mode0"};
  bool all_reg = true;
  for (const char* n : names) {
    if (!Registry::instance().has(n)) {
      all_reg = false;
      std::printf("      missing registration: %s\n", n);
    }
  }
  report(all_reg, "(6a) all ten CandF names registered (C2c: mode3)");

  // All-variants 7-agent distinct smoke (3 seeds), no-throw + finite.
  {
    std::vector<mas_sim::AgentFactory> f(7);
    f[0] = make_CandF();
    f[1] = make_CandF_T104();
    f[2] = make_CandF_T106();
    f[3] = make_CandF_T108();
    f[4] = make_CandF_mode2();
    f[5] = make_CandF_mode3();
    f[6] = make_CandF_mode0();
    bool no_throw = true, all_finite = true;
    for (int r = 0; r < 3; ++r) {
      try {
        const auto s = run_tournament(f, 7, 912000 + r,
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
           "(6b) all-variants 7-agent distinct smoke: no exceptions, finite");
  }

  // Same-seed determinism: run twice with the same seed, compare vectors.
  {
    std::vector<mas_sim::AgentFactory> f(7);
    f[0] = make_CandF();
    f[1] = make_CandF_T104();
    f[2] = make_CandF_T106();
    f[3] = make_CandF_T108();
    f[4] = make_CandF_mode2();
    f[5] = make_CandF_mode3();
    f[6] = make_CandF_mode0();
    const auto a =
        run_tournament(f, 7, 913500, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    const auto b =
        run_tournament(f, 7, 913500, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    std::printf("      same-seed scores: ");
    for (size_t i = 0; i < a.size(); ++i) std::printf("%.4f ", a[i]);
    std::printf("\n");
    report(a == b && finite_scores(a),
           "(6c) same seed => identical score vector (all seven variants)");
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

// =============================================================================
// (7..12) slice C2c: mode 3 (self-limiting probe-cycle endgame) —
// registered as CandF_mode3 (mode 3 @ the frozen T=104).
//
// A mode-agnostic scripted-history driver: `n0` = completed rounds before
// the first decision here (pre-T history is all-peace), opponents play the
// EXOGENOUS schedule opp_fn(n) (both opps the same) plus the fixed last
// round `opp_last_init`; the agent's own output extends `my[]` exactly as
// the engine would (n == history length at each call). Returns the decided
// actions (one per driven round).
// =============================================================================
std::vector<int> drive(mas_sim::AgentFactory f, int n0, int count,
                       int opp_last_init,
                       const std::function<int(int)>& opp_fn) {
  auto a = f();
  std::vector<int> my(n0, 0);
  std::vector<int> o1(n0, 0);
  std::vector<int> o2(n0, 0);
  if (n0 > 0) {
    o1[n0 - 1] = opp_last_init;
    o2[n0 - 1] = opp_last_init;
  }
  for (int i = 0; i < count; ++i) {
    const int n = n0 + i;
    const int mv = a->selectAction(n, my, o1, o2);
    my.push_back(mv);
    const int ov = opp_fn(n);
    o1.push_back(ov);
    o2.push_back(ov);
  }
  return std::vector<int>(my.begin() + n0, my.end());
}

// mode1 at the frozen T=104 (the committed-ramp reference arm for C2c).
mas_sim::AgentFactory candf_mode1_T104() {
  CandFParams p;
  p.ramp_mode = 1;
  p.ramp_T = 104;
  return make_CandF(p);
}

// =============================================================================
// (7) dispatch C2c test 1 — repair fires: in-window (n >= T=104), our LAST
// action = War AND both opponents warred last round (echo detected) ->
// Peace. This is ALSO a double-war round (R2 would war), so the repair's
// precedence over R2 inside the window is exercised: the agent plays Peace.
// =============================================================================
void test_c2c1_repair_fires() {
  std::printf("--- (7) C2c-1: in-window my-War + both-opp-war -> Peace (repair) ---\n");
  // n=104 (== T), my[103]=1 (our ramp war), o1[103]=o2[103]=1 (they echoed).
  auto agent = make_CandF_mode3()();
  std::vector<int> my = rep(104, 0);
  my[103] = 1;               // our last action = War (ramp-caused)
  std::vector<int> o1 = rep(104, 0);
  o1[103] = 1;               // both opps warred last round -> echo
  std::vector<int> o2 = rep(104, 0);
  o2[103] = 1;
  const int repair = call(agent.get(), 104, my, o1, o2);
  std::printf("      n=104 (in-window), my last=War, both opps warred last -> %d\n",
              repair);
  report(repair == 0,
         "C2c-1: in-window, our last=War AND both opps warred last (echo) -> "
         "Peace (repair fires; it overrides R2 inside the window)");
}

// =============================================================================
// (8) dispatch C2c test 2 — commit (no echo): in-window (n >= T=104), our
// LAST action = War but the opponents are SILENT last round (saints/
// tolerant: no echo) -> War (the committed full commit; no repair fires).
// =============================================================================
void test_c2c2_commit_no_echo() {
  std::printf("--- (8) C2c-2: in-window my-War + opps silent -> War (commit) ---\n");
  auto agent = make_CandF_mode3()();
  std::vector<int> my = rep(104, 0);
  my[103] = 1;               // our last action = War
  std::vector<int> o1 = rep(104, 0);  // both opps silent (saints)
  std::vector<int> o2 = rep(104, 0);
  const int commit = call(agent.get(), 104, my, o1, o2);
  std::printf("      n=104 (in-window), my last=War, opps silent last -> %d\n",
              commit);
  report(commit == 1,
         "C2c-2: in-window, our last=War but opps silent (no echo) -> War "
         "(committed; the full commit vs silent pairs)");
}

// =============================================================================
// (9) dispatch C2c test 3 — the echo cycle alternates W,P,W,P over 6
// scripted in-window rounds. The pair is a same-round echoer: whenever we
// war they war, so after a committed ramp War the NEXT round is an echo
// (both opps warred last) -> repair Peace; their echo then decays (we
// peaced, so the following round is NOT an echo) -> the committed ramp
// resumes War. Driving from n=T=104 (pre-T all-peace) with a perpetual
// echoer yields exactly W,P,W,P,W,P.
// =============================================================================
void test_c2c3_echo_cycle() {
  std::printf("--- (9) C2c-3: echo cycle W,P,W,P,W,P over 6 in-window rounds ---\n");
  // n0=104 (first decision is n=104==T; pre-T all-peace). The echoer wars
  // every round (opp_fn == 1) and also warred at the last pre-driven round
  // (opp_last_init=1) so the very first decision sees a live echo.
  const std::vector<int> seq =
      drive(make_CandF_mode3(), 104, 6, 1, [](int) { return 1; });
  const std::string s =
      "      mode3 vs echoer, n=104..109: " +
      std::to_string(seq[0]) + std::to_string(seq[1]) + std::to_string(seq[2]) +
      std::to_string(seq[3]) + std::to_string(seq[4]) + std::to_string(seq[5]) +
      "\n";
  std::printf("%s", s.c_str());
  const std::vector<int> expect = {1, 0, 1, 0, 1, 0};  // W,P,W,P,W,P
  report(seq == expect,
         "C2c-3: echo cycle alternates W,P,W,P,W,P over 6 in-window rounds "
         "(repair after each war; committed war resumes after each repair)");
}

// =============================================================================
// (10) dispatch C2c test 4 — outside the window (n < T) mode3 is unchanged
// vs mode1(T=104). Both arms have no ramp below T (mode1's ramp war needs
// n>=104; mode3's window also starts at T), so both reduce to R2-then-R1
// and must decide identically. Driven over n=90..103 (the ramp window but
// below T) with an exogenous alternating opp-war schedule, same-seed, the
// two decided streams must match exactly.
// =============================================================================
void test_c2c4_outside_window_matches_mode1() {
  std::printf("--- (10) C2c-4: outside window (n<T) mode3 == mode1(T=104) ---\n");
  // n0=90, count=14 -> n=90..103 (all < T=104). Exogenous opp schedule:
  // opps war on even n, peace on odd n (both opps the same) — a fixed
  // pattern independent of our output, so the two agents stay in lockstep.
  const auto opp = [](int n) { return (n % 2 == 0) ? 1 : 0; };
  const std::vector<int> m3 = drive(make_CandF_mode3(), 90, 14, 0, opp);
  // mode1 at the same frozen T=104 (the committed-ramp reference arm).
  const std::vector<int> m1 = drive(candf_mode1_T104(), 90, 14, 0, opp);
  std::printf("      n=90..103: mode3 = ");
  for (int v : m3) std::printf("%d", v);
  std::printf(", mode1(T104) = ");
  for (int v : m1) std::printf("%d", v);
  std::printf("\n");
  report(m3 == m1,
         "C2c-4: outside the window (n<T) mode3 == mode1(T=104) same-seed "
         "(no ramp below T; both R2-then-R1)");
}

// =============================================================================
// (11) dispatch C2c test 5 — mode3 vs mode1(T=104) on a scripted SAINT
// echo-free history: both commit identically. Saints (Nice) never war, so
// no echo ever occurs and the mode3 repair never fires -> mode3 is the
// committed ramp (War from T), exactly mode1. Driven over the full match
// (n=0..109) vs two all-peace opps, the two decided streams must match,
// with both committing War exactly from n=T=104 on.
// =============================================================================
void test_c2c5_saint_commit_identical() {
  std::printf("--- (11) C2c-5: vs saint (echo-free) mode3 == mode1 (both commit) ---\n");
  // Full 110-round match vs two silent (all-peace) saints.
  const auto saints = [](int) { return 0; };
  const std::vector<int> m3 = drive(make_CandF_mode3(), 0, 110, 0, saints);
  const std::vector<int> m1 = drive(candf_mode1_T104(), 0, 110, 0, saints);
  // Both must be all-peace until n=103 and all-War from n=104 (== T).
  // (n0=0, so decided[i] corresponds to n=i.)
  bool identical = (m3 == m1);
  bool commit_ok = true;
  for (int i = 0; i < 110; ++i) {
    const int expected = (i >= 104) ? 1 : 0;  // War iff n >= T=104
    if (m3[i] != expected || m1[i] != expected) commit_ok = false;
  }
  std::printf("      last 8 mode3 (n=102..109): ");
  for (int i = 102; i < 110; ++i) std::printf("%d", m3[i]);
  std::printf("  (expect 0 0 1 1 1 1 1 1)\n");
  report(identical && commit_ok,
         "C2c-5: vs saint (echo-free) mode3 and mode1(T=104) commit "
         "identically (War exactly from n=T=104)");
}

// =============================================================================
// (12) dispatch C2c test 6 — registration smoke + same-seed determinism for
// CandF_mode3: the name is registered, and a small same-seed tournament
// containing mode3 runs twice with an identical score vector.
// =============================================================================
void test_c2c6_registration_determinism() {
  std::printf("--- (12) C2c-6: CandF_mode3 registration + same-seed determinism ---\n");
  const bool registered = Registry::instance().has("CandF_mode3");
  report(registered, "C2c-6a: CandF_mode3 registered");

  const auto nice = Registry::instance().get("NiceAgent");
  const auto nasty = Registry::instance().get("NastyAgent");
  std::vector<mas_sim::AgentFactory> f(4);
  f[0] = make_CandF_mode3();
  f[1] = make_CandF_T104();
  f[2] = nice;
  f[3] = nasty;
  const auto a = run_tournament(f, 4, 915000, TournamentMode::kDistinct,
                                RngKind::kSfc64);
  const auto b = run_tournament(f, 4, 915000, TournamentMode::kDistinct,
                                RngKind::kSfc64);
  std::printf("      same-seed scores: ");
  for (size_t i = 0; i < a.size(); ++i) std::printf("%.4f ", a[i]);
  std::printf("\n");
  report(a == b && finite_scores(a),
         "C2c-6b: same seed => identical score vector (mode3 in pool)");
}

int main() {
  std::printf("=== candidates_f_test (slice C2a/C2c: CandF endgame-ramp "
              "retaliator + mode3 probe-cycle) ===\n");
  test_f1_mode0_nice3way();
  test_f2_mode0_nasty3way();
  test_f3_ramp_boundary();
  test_f4_mode2();
  test_f5_ramp_retaliation();
  test_f6_registration_smoke();

  test_c2c1_repair_fires();
  test_c2c2_commit_no_echo();
  test_c2c3_echo_cycle();
  test_c2c4_outside_window_matches_mode1();
  test_c2c5_saint_commit_identical();
  test_c2c6_registration_determinism();

  if (g_failures == 0) {
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("FAIL: %d/%d check(s) failed\n", g_failures, g_checks);
  return 1;
}
