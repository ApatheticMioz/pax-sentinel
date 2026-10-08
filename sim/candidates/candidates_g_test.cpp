// =============================================================================
// sim/candidates/candidates_g_test.cpp — slice C2d: behavioral tests for
// candidate G "CandG_TolRamp" (the "coast + harvest" hybrid: TolerantW5
// backbone + committed endgame war ramp).
//
// Mirrors the candidates_f test layout:
//   (1) backbone equivalence: CandG with ramp_T = 111 (never fires: > max
//       horizon 110) vs the zoo TolerantW5 — IDENTICAL action sequences over
//       scripted histories, including:
//         (a) the zoo test's exact TolerantW5 cases (zoo_t01_test.cpp,
//             lines 224-226) plus n==0 and short-history (n < 5) cases;
//         (b) a randomized 110-round drive (10 seeds) over both opponents'
//             full histories — the last-5 window slides, so the war>peace
//             tally flips peace<->war as wars enter/leave the window
//             (the "cumulative-flip" cases). CandG(111) must match TolerantW5
//             bit-for-bit on the ENTIRE 110-action sequence.
//   (2) ramp fires at n = 104 vs a peaceful pair: scripted all-peace drive —
//       peace for n < 104 (backbone: 0 war in the last-5 window), committed
//       war for n >= 104; plus a real same-seed tournament vs two Nice.
//   (3) committed inside the window (no suppression below n >= 104): once
//       n >= 104 the ramp wars unconditionally even when the TolerantW5
//       windowed decision would be peace (a peace-dominant last-5), and the
//       ramp does NOT fire below n = 104 (pure backbone there).
//   (4) registration smoke + same-seed determinism (the arm is fully
//       deterministic — no probes, no RNG draw — so a same-seed tournament
//       is reproducible exactly).
//
// Round-index convention: n == number of completed rounds; histories hold
// rds 0..n-1; "rd k" is the 0-based index k.
//
// Build: sim/build.sh `candidatesg` / `candidatesgasan` (this file is the
// CandG test main; it links engine.cpp + zoo_t01.cpp (TolerantW5 + anchor
// bots) + candidates_g.cpp — mirroring the candidatesf target).
// =============================================================================

#include "candidates_g.hpp"

#include "../zoo/zoo_common.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using mas_sim::AgentBase;
using mas_sim::CandGParams;
using mas_sim::Rng;
using mas_sim::RngKind;
using mas_sim::Registry;
using mas_sim::TournamentMode;
using mas_sim::make_CandG;
using mas_sim::make_CandG_TolRamp;
using mas_sim::run_tournament;

namespace {

int g_failures = 0;
int g_checks = 0;

void report(bool ok, const char* name) {
  ++g_checks;
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) ++g_failures;
}

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

// Full-match drive: the agent decides at each n (0..H-1); both opponents
// play the given exogenous full histories (prefixes passed in). Returns the
// agent's decided action per round. `H` = number of rounds (horizon).
std::vector<int> drive_seq(mas_sim::AgentFactory f, const std::vector<int>& o1full,
                           const std::vector<int>& o2full) {
  auto a = f();
  const int H = static_cast<int>(o1full.size());
  std::vector<int> my, o1, o2, out;
  my.reserve(H);
  o1.reserve(H);
  o2.reserve(H);
  out.reserve(H);
  for (int n = 0; n < H; ++n) {
    const int act = a->selectAction(n, my, o1, o2);
    out.push_back(act);
    my.push_back(act);
    o1.push_back(o1full[n]);
    o2.push_back(o2full[n]);
  }
  return out;
}

}  // namespace

// =============================================================================
// (1) Backbone equivalence: CandG(ramp_T=111, never fires) == zoo TolerantW5.
// =============================================================================
void test_g1_backbone_equivalence() {
  std::printf("--- (1) backbone equivalence: CandG(T=111) vs zoo TolerantW5 ---\n");

  const auto toler = Registry::instance().get("TolerantW5");
  const CandGParams never{111};  // 111 > max horizon 110 -> ramp never fires

  // (1a) the zoo test's exact TolerantW5 cases (zoo_t01_test.cpp:224-226),
  //      plus n==0 and two short-history (n < 5, window-clamped) cases.
  //      For each, both CandG(111) and TolerantW5 must agree AND match the
  //      zoo's documented expected action.
  struct Case {
    int n;
    std::vector<int> my;
    std::vector<int> o1;
    std::vector<int> o2;
    int expect;  // the zoo-documented TolerantW5 action
  };
  const Case cases[] = {
      // n==0 (R0; zoo returns 0).
      {0, {}, {}, {}, 0},
      // zoo_t01_test.cpp exact cases (window = last 5 = full history here):
      //   last 5: o1=0,0,1,1,1  o2=0,0,0,0,0 -> 3 war vs 7 peace -> peace
      {5, {0, 0, 0, 0, 0}, {0, 0, 1, 1, 1}, {0, 0, 0, 0, 0}, 0},
      //   last 5: o1=1,1,1,0,0  o2=1,1,0,0,0 -> 5 war vs 5 -> tie -> peace
      {5, {0, 0, 0, 0, 0}, {1, 1, 1, 0, 0}, {1, 1, 0, 0, 0}, 0},
      //   last 5: o1=1,1,1,1,0  o2=1,1,1,0,0 -> 6 war vs 4 -> war
      {5, {0, 0, 0, 0, 0}, {1, 1, 1, 1, 0}, {1, 1, 1, 0, 0}, 1},
      // short history (n < 5): window clamps to [0, n); one opp warred once
      // (1 war vs 3 peace across two opps) -> peace.
      {2, {0, 0}, {0, 1}, {0, 0}, 0},
      // short history (n < 5): both opps warred both rounds (4 war vs 0)
      // -> war (window-clamped rescan matches TolerantW5).
      {2, {0, 0}, {1, 1}, {1, 1}, 1},
  };
  {
    auto g = make_CandG(never)();
    auto z = toler();
    bool all_ok = true;
    for (const Case& c : cases) {
      const int gv = call(g.get(), c.n, c.my, c.o1, c.o2);
      const int zv = call(z.get(), c.n, c.my, c.o1, c.o2);
      const bool ok = (gv == zv) && (zv == c.expect);
      all_ok = all_ok && ok;
      std::printf("      n=%d: CandG(111)=%d, TolerantW5=%d, expect=%d %s\n",
                  c.n, gv, zv, c.expect, ok ? "ok" : "MISMATCH");
    }
    report(all_ok,
           "(1a) scripted points (zoo cases + n==0 + n<5): CandG(111) == "
           "TolerantW5 == documented action");
  }

  // (1b) randomized 110-round drives (the "cumulative-flip" cases): 10
  //      seeds, both opponents' full 110-round histories drawn from a
  //      throwaway Rng (independent of the engine's fast_rng, so the test
  //      is fully reproducible). The last-5 window slides over these, so a
  //      war that was inside the window at round k leaves it by round k+5
  //      and the war>peace tally flips both ways; CandG(111) (ramp off)
  //      must reproduce TolerantW5's ENTIRE 110-action sequence per seed.
  {
    bool all_ok = true;
    int total_flips = 0;
    for (int seed = 0; seed < 10; ++seed) {
      // Draw the two opponents' full 110-round histories deterministically.
      std::vector<int> o1full(110), o2full(110);
      Rng g(seed * 7919 + 13);
      for (int n = 0; n < 110; ++n) {
        o1full[n] = static_cast<int>(g.next32() & 1u);
        o2full[n] = static_cast<int>(g.next32() & 1u);
      }
      const std::vector<int> sg = drive_seq(make_CandG(never), o1full, o2full);
      const std::vector<int> sz = drive_seq(toler, o1full, o2full);
      const bool ok = (sg == sz);
      all_ok = all_ok && ok;
      // Count the peace<->war flips in the decided sequence (evidence the
      // windowed tally really flipped over the drive).
      int flips = 0;
      for (int i = 1; i < 110; ++i)
        if (sz[i] != sz[i - 1]) ++flips;
      total_flips += flips;
      std::printf("      seed %d: |CandG(111)|==|TolerantW5|=%d, actions=%d, "
                  "flips=%d %s\n",
                  seed, ok, static_cast<int>(sz.size()), flips,
                  ok ? "identical" : "DIFFER");
    }
    report(all_ok && total_flips > 0,
           "(1b) randomized 110-round drives (10 seeds): CandG(111) identical "
           "to TolerantW5 across every windowed cumulative-flip");
  }
}

// =============================================================================
// (2) Ramp fires at n = 104 vs a peaceful pair.
// =============================================================================
void test_g2_ramp_fires() {
  std::printf("--- (2) ramp fires at n=104 vs a peaceful pair ---\n");

  // (2a) scripted all-peace drive (both opponents always peace): for n<104
  //      the TolerantW5 backbone reads 0 war in the last-5 window -> peace;
  //      for n>=104 the committed ramp forces war. Assert the exact boundary.
  {
    auto a = make_CandG_TolRamp()();
    bool below_ok = true, at_ok = true;
    for (int n = 0; n < 104; ++n) {
      const int v = call(a.get(), n, rep(n, 0), rep(n, 0), rep(n, 0));
      if (v != 0) below_ok = false;
    }
    for (int n = 104; n <= 109; ++n) {
      const int v = call(a.get(), n, rep(n, 0), rep(n, 0), rep(n, 0));
      if (v != 1) at_ok = false;
    }
    // Spot-check the boundary round pair (n=103 vs n=104).
    const int b103 = call(a.get(), 103, rep(103, 0), rep(103, 0), rep(103, 0));
    const int b104 = call(a.get(), 104, rep(104, 0), rep(104, 0), rep(104, 0));
    std::printf("      all-peace: n=103 -> %d, n=104 -> %d (ramp boundary)\n",
                b103, b104);
    report(below_ok && at_ok && b103 == 0 && b104 == 1,
           "(2a) ramp fires at n=104: peace for n<104, committed war for "
           "n>=104 (all-peace pair)");
  }

  // (2b) real same-seed tournament vs two Nice (peaceful pair): the ramp
  //      harvests the all-peace payoffs (war vs two peace = 6/rd on the
  //      rounds >= 104, 4/rd elsewhere) — must run without throw, finite,
  //      and score at or above the all-peace 4/rd baseline (the harvest is
  //      pure upside vs a peaceful pair).
  {
    const auto nice = Registry::instance().get("NiceAgent");
    bool no_throw = true, all_finite = true;
    double avg = 0.0;
    for (int r = 0; r < 3; ++r) {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = make_CandG_TolRamp();
      f[1] = nice;
      f[2] = nice;
      try {
        const auto s = run_tournament(f, 3, 950000 + r,
                                      TournamentMode::kDistinct,
                                      RngKind::kSfc64);
        if (!finite_scores(s)) all_finite = false;
        avg += s[0];
      } catch (const std::exception& e) {
        std::printf("      run %d threw: %s\n", r, e.what());
        no_throw = false;
        all_finite = false;
      }
    }
    avg /= 3;
    std::printf("      CandG_TolRamp vs {Nice, Nice} 3-run avg = %.4f/rd "
                "(>= 4.0 all-peace baseline expected)\n",
                avg);
    report(no_throw && all_finite && avg >= 4.0 - 1e-9,
           "(2b) real same-seed tournament vs {Nice,Nice}: no throw, finite, "
           "score >= all-peace 4/rd baseline (harvest)");
  }
}

// =============================================================================
// (3) Committed inside the window (no suppression below n >= 104).
// =============================================================================
void test_g3_committed() {
  std::printf("--- (3) committed inside the window (no suppression below "
              "n>=104) ---\n");

  // (3a) In-window, the ramp OVERRIDES a would-be backbone PEACE: craft an
  //      n=105 history whose last-5 window is peace-dominant (TolerantW5
  //      would play peace) — the committed ramp must still war.
  {
    auto a = make_CandG_TolRamp()();
    // Last-5 window = indices 100..104; each opp has 1 war in it:
    //   o1[100..104] = 0,0,0,0,1  (1 war) ; o2[100..104] = 0,0,0,0,1 (1 war)
    //   -> war = 2, peace = 8 -> TolerantW5 would PEACE (2 > 8 false).
    std::vector<int> o1 = rep(105, 0);
    o1[104] = 1;
    std::vector<int> o2 = rep(105, 0);
    o2[104] = 1;
    const int v = call(a.get(), 105, rep(105, 0), o1, o2);
    // Control: the same history at n=104 (last-5 = 99..103, all peace) —
    // the backbone would also peace, ramp wars.
    std::vector<int> o1b = rep(104, 0);
    std::vector<int> o2b = rep(104, 0);
    const int vb = call(a.get(), 104, rep(104, 0), o1b, o2b);
    std::printf("      in-window peace-dominant last-5: n=105 -> %d, "
                 "n=104 (all-peace last-5) -> %d (ramp commits war)\n",
                 v, vb);
    report(v == 1 && vb == 1,
           "(3a) in-window committed: peace-dominant last-5 (backbone would "
           "peace) still wars at n>=104 (ramp overrides R1)");
  }

  // (3b) In-window, the ramp also holds when the backbone would WAR
  //      (war-dominant last-5) — both paths agree on war, so the commit is
  //      stable (not a one-way override that could be undone by R1).
  {
    auto a = make_CandG_TolRamp()();
    // Last-5 = indices 101..105; each opp 3 wars:
    //   o1[101..105] = 1,1,1,0,0 ; o2[101..105] = 1,1,1,0,0
    //   -> war = 6, peace = 4 -> TolerantW5 would WAR (6 > 4 true).
    std::vector<int> o1 = rep(106, 0);
    for (int i = 101; i <= 103; ++i) o1[i] = 1;
    std::vector<int> o2 = rep(106, 0);
    for (int i = 101; i <= 103; ++i) o2[i] = 1;
    const int v = call(a.get(), 106, rep(106, 0), o1, o2);
    std::printf("      in-window war-dominant last-5: n=106 -> %d\n", v);
    report(v == 1,
           "(3b) in-window committed: war-dominant last-5 also wars (stable "
           "commit; R1 cannot undo the ramp)");
  }

  // (3c) Below the window (n < 104) there is NO ramp — the arm is the pure
  //      TolerantW5 backbone (no premature war / no "suppression").
  //      n=103 all-peace -> backbone peace (0).
  {
    auto a = make_CandG_TolRamp()();
    const int v103 = call(a.get(), 103, rep(103, 0), rep(103, 0), rep(103, 0));
    // n=103 with a peace-dominant last-5 (backbone peace) — still peace.
    std::vector<int> o1 = rep(103, 0);
    o1[102] = 1;
    std::vector<int> o2 = rep(103, 0);
    o2[102] = 1;
    const int v103b = call(a.get(), 103, rep(103, 0), o1, o2);
    std::printf("      below window: n=103 all-peace -> %d, "
                 "n=103 peace-dominant last-5 -> %d (pure backbone, no ramp)\n",
                v103, v103b);
    report(v103 == 0 && v103b == 0,
           "(3c) below the window (n<104) no ramp: pure TolerantW5 backbone "
           "(peace-dominant last-5 -> peace)");
  }
}

// =============================================================================
// (4) Registration smoke + same-seed determinism.
// =============================================================================
void test_g4_registration_determinism() {
  std::printf("--- (4) registration smoke + same-seed determinism ---\n");

  // (4a) the registered name resolves.
  const bool registered = Registry::instance().has("CandG_TolRamp");
  report(registered, "(4a) CandG_TolRamp registered in the registry");

  // (4b) same-seed determinism: the arm is fully deterministic (no probes,
  //      no RNG draw) — a tournament with a mix of deterministic and
  //      stochastic anchors, run twice with the SAME seed, must yield an
  //      identical score vector (the horizon draw is the only RNG, and it
  //      is re-seeded identically).
  {
    const auto nice = Registry::instance().get("NiceAgent");
    const auto nasty = Registry::instance().get("NastyAgent");
    const auto rand_ = Registry::instance().get("RandomAgent");
    const auto tft = Registry::instance().get("TFT_any");
    std::vector<mas_sim::AgentFactory> f(5);
    f[0] = make_CandG_TolRamp();
    f[1] = nice;
    f[2] = nasty;
    f[3] = rand_;
    f[4] = tft;

    bool no_throw = true, all_finite = true;
    const auto a = run_tournament(f, 5, 960000, TournamentMode::kDistinct,
                                  RngKind::kSfc64);
    const auto b = run_tournament(f, 5, 960000, TournamentMode::kDistinct,
                                  RngKind::kSfc64);
    try {
      // A few independent seeds: each must be finite and no-throw.
      for (int r = 0; r < 3; ++r) {
        const auto s = run_tournament(f, 5, 961000 + r,
                                      TournamentMode::kDistinct,
                                      RngKind::kSfc64);
        if (!finite_scores(s)) all_finite = false;
      }
    } catch (const std::exception& e) {
      std::printf("      smoke run threw: %s\n", e.what());
      no_throw = false;
      all_finite = false;
    }
    std::printf("      same-seed (960000) scores: ");
    for (size_t i = 0; i < a.size(); ++i) std::printf("%.4f ", a[i]);
    std::printf("\n");
    report(registered && no_throw && all_finite && a == b && finite_scores(a),
           "(4b) same seed => identical score vector (deterministic arm: no "
           "probe, no RNG draw); 3 more seeds finite");
  }
}

int main() {
  std::printf("=== candidates_g_test (slice C2d: CandG_TolRamp — TolerantW5 "
              "backbone + committed endgame war ramp) ===\n");
  test_g1_backbone_equivalence();
  test_g2_ramp_fires();
  test_g3_committed();
  test_g4_registration_determinism();

  if (g_failures == 0) {
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("FAIL: %d/%d check(s) failed\n", g_failures, g_checks);
  return 1;
}
