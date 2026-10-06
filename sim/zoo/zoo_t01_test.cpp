// =============================================================================
// sim/zoo/zoo_t01_test.cpp — slice B2: behavioral smoke tests for the zoo
//
// Two groups:
//   (i)  scripted-history unit tests: build a bot directly and call
//        selectAction() with crafted myHistory/opp1/opp2 vectors, asserting
//        the exact expected action. Stochastic bots are made deterministic by
//        reseeding the engine's fast_rng() and predicting the draw with a
//        local throwaway Rng (same algorithm, same seed).
//   (ii) smoke tournament: every registered zoo bot in one pool (30+ agents),
//        5 runs, fast mode; assert all scores finite, no crash, plus two
//        sanity inequalities (see below).
//
// Build: see sim/build.sh `zoo` target (links engine.cpp + zoo_t01.cpp).
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

// Predict the value zoo_draw_bit() would produce for a freshly-reseeded
// fast_rng(seed): the bit is rand()<16384 ? 0 : 1 from a throwaway Rng.
int predict_bit(int seed) {
  Rng lr(seed);
  return (lr.rand() < 16384) ? 0 : 1;
}

// Predict zoo_draw_uniform() for a freshly-reseeded fast_rng(seed).
double predict_uniform(int seed) {
  Rng lr(seed);
  return lr.uniform();
}

// ---------------------------------------------------------------------------
// Group (i): scripted-history unit tests.
// ---------------------------------------------------------------------------

struct Hist {
  int n;
  std::vector<int> my;
  std::vector<int> o1;
  std::vector<int> o2;
};

int call(mas_sim::AgentBase* a, const Hist& h) {
  // Copy to non-const vectors (selectAction takes non-const refs).
  std::vector<int> my(h.my), o1(h.o1), o2(h.o2);
  return a->selectAction(h.n, my, o1, o2);
}

void test_unit_deterministic() {
  std::printf("--- scripted-history unit tests (deterministic bots) ---\n");

  // NiceAgent: always 0.
  {
    auto a = Registry::instance().get("NiceAgent")();
    report(call(a.get(), {0, {}, {}, {}}) == 0 &&
           call(a.get(), {5, {0, 0}, {1, 1}, {1, 0}}) == 0,
           "NiceAgent always 0");
  }

  // NastyAgent: always 1.
  {
    auto a = Registry::instance().get("NastyAgent")();
    report(call(a.get(), {0, {}, {}, {}}) == 1 &&
           call(a.get(), {3, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}) == 1,
           "NastyAgent always 1");
  }

  // TFT_any: war iff EITHER opp warred last; opens peace.
  {
    auto a = Registry::instance().get("TFT_any")();
    const bool ok =
        call(a.get(), {0, {}, {}, {}}) == 0 &&
        call(a.get(), {1, {0}, {1}, {0}}) == 1 &&  // opp1 warred
        call(a.get(), {1, {0}, {0}, {1}}) == 1 &&  // opp2 warred
        call(a.get(), {1, {0}, {0}, {0}}) == 0;    // both peace
    report(ok, "TFT_any: any-opponent last-war");
  }

  // TFT_maj: war iff BOTH warred last.
  {
    auto a = Registry::instance().get("TFT_maj")();
    const bool ok =
        call(a.get(), {1, {0}, {1}, {1}}) == 1 &&
        call(a.get(), {1, {0}, {1}, {0}}) == 0 &&
        call(a.get(), {1, {0}, {0}, {1}}) == 0 &&
        call(a.get(), {1, {0}, {0}, {0}}) == 0;
    report(ok, "TFT_maj: both-opp last-war");
  }

  // TF2T_any: war iff either opp warred on BOTH of the last 2 rds.
  {
    auto a = Registry::instance().get("TF2T_any")();
    const bool ok =
        call(a.get(), {2, {0, 0}, {1, 1}, {0, 0}}) == 1 &&  // opp1: WW
        call(a.get(), {2, {0, 0}, {0, 0}, {1, 1}}) == 1 &&  // opp2: WW
        call(a.get(), {2, {0, 0}, {1, 0}, {0, 0}}) == 0 &&  // opp1: WO (not WW)
        call(a.get(), {1, {0}, {1}, {0}}) == 0;             // n<2
    report(ok, "TF2T_any: either-opp two-war window");
  }

  // TF2T_maj: both opps warred both of the last 2.
  {
    auto a = Registry::instance().get("TF2T_maj")();
    const bool ok =
        call(a.get(), {2, {0, 0}, {1, 1}, {1, 1}}) == 1 &&
        call(a.get(), {2, {0, 0}, {1, 1}, {0, 0}}) == 0 &&  // only opp1 WW
        call(a.get(), {2, {0, 0}, {0, 0}, {1, 1}}) == 0;    // only opp2 WW
    report(ok, "TF2T_maj: both-opp two-war window");
  }

  // Grim_any: permanent after first war by either (stateless rescan).
  {
    auto a = Registry::instance().get("Grim_any")();
    const bool ok =
        call(a.get(), {0, {}, {}, {}}) == 0 &&
        call(a.get(), {3, {0, 0, 0}, {0, 1, 0}, {0, 0, 0}}) == 1 &&  // opp1 war at i=1
        call(a.get(), {3, {0, 0, 0}, {0, 0, 0}, {0, 0, 1}}) == 1 &&  // opp2 war at i=2
        call(a.get(), {4, {0, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}}) == 1;
    report(ok, "Grim_any: permanent after any lifetime war");
  }

  // Grim_maj: permanent after first round where BOTH warred.
  {
    auto a = Registry::instance().get("Grim_maj")();
    const bool ok =
        call(a.get(), {3, {0, 0, 0}, {0, 1, 0}, {0, 0, 0}}) == 0 &&  // only opp1 war (never both)
        call(a.get(), {3, {0, 0, 0}, {0, 1, 0}, {0, 1, 0}}) == 1 &&  // both war at i=1
        call(a.get(), {3, {0, 0, 0}, {0, 1, 0}, {0, 0, 1}}) == 0;    // opps war in different rounds
    report(ok, "Grim_maj: permanent after a both-war round");
  }

  // WSLS_naive: repeat last iff last payoff>=2 else switch; opens peace.
  //   prev=0, opps both 0 -> payoff kZooPayoff[0][0][0]=4 (>=2) -> stay 0
  //   prev=0, opps both 1 -> payoff kZooPayoff[0][1][1]=-1 (<2) -> switch to 1
  //   prev=1, opps both 0 -> payoff kZooPayoff[1][0][0]=6 (>=2) -> stay 1
  {
    auto a = Registry::instance().get("WSLS_naive")();
    const bool ok =
        call(a.get(), {0, {}, {}, {}}) == 0 &&
        call(a.get(), {1, {0}, {0}, {0}}) == 0 &&  // 4 -> stay peace
        call(a.get(), {1, {0}, {1}, {1}}) == 1 &&  // -1 -> switch to war
        call(a.get(), {1, {1}, {0}, {0}}) == 1;    // 6 -> stay war
    report(ok, "WSLS_naive: payoff-threshold switch");
  }

  // Alternator: P,W,P,W,...
  {
    auto a = Registry::instance().get("Alternator")();
    const bool ok = call(a.get(), {0, {}, {}, {}}) == 0 &&
                    call(a.get(), {1, {}, {}, {}}) == 1 &&
                    call(a.get(), {2, {}, {}, {}}) == 0 &&
                    call(a.get(), {3, {}, {}, {}}) == 1;
    report(ok, "Alternator: P,W,P,W");
  }

  // PeriodicPPW: P,P,W repeating.
  {
    auto a = Registry::instance().get("PeriodicPPW")();
    const bool ok = call(a.get(), {0, {}, {}, {}}) == 0 &&  // 0%3=0
                    call(a.get(), {1, {}, {}, {}}) == 0 &&  // 1%3=1
                    call(a.get(), {2, {}, {}, {}}) == 1 &&  // 2%3=2
                    call(a.get(), {3, {}, {}, {}}) == 0 &&  // 3%3=0
                    call(a.get(), {5, {}, {}, {}}) == 1 &&  // 5%3=2
                    call(a.get(), {8, {}, {}, {}}) == 1;    // 8%3=2
    report(ok, "PeriodicPPW: P,P,W cycle");
  }

  // BrokenTFT_1: reads ONLY opp1's last move, ignores opp2; opens peace.
  {
    auto a = Registry::instance().get("BrokenTFT_1")();
    const bool ok =
        call(a.get(), {0, {}, {}, {}}) == 0 &&
        call(a.get(), {1, {0}, {1}, {0}}) == 1 &&  // opp1 war -> war
        call(a.get(), {1, {0}, {0}, {1}}) == 0 &&  // opp2 war IGNORED
        call(a.get(), {1, {0}, {0}, {0}}) == 0;
    report(ok, "BrokenTFT_1: only opponent-1 last move");
  }

  // TolerantAgent (tier 0): war iff cumulative war>peace across BOTH opps.
  //   opp1 = 0,0,0,1  opp2 = 0,0,0,0 : 1 war vs 7 peace -> peace
  //   opp1 = 1,1,1,0  opp2 = 1,0,0,0 : 4 war vs 4 peace -> tie -> peace (not >)
  //   opp1 = 1,1,1,1  opp2 = 1,1,0,0 : 6 war vs 2 peace -> war
  {
    auto a = Registry::instance().get("TolerantAgent")();
    const bool ok =
        call(a.get(), {0, {}, {}, {}}) == 0 &&
        call(a.get(), {4, {0, 0, 0, 0}, {0, 0, 0, 1}, {0, 0, 0, 0}}) == 0 &&
        call(a.get(), {4, {0, 0, 0, 0}, {1, 1, 1, 0}, {1, 0, 0, 0}}) == 0 &&
        call(a.get(), {4, {0, 0, 0, 0}, {1, 1, 1, 1}, {1, 1, 0, 0}}) == 1;
    report(ok, "TolerantAgent: cumulative war>peace both opps");
  }

  // TolerantW5: windowed to last 5.
  //   last 5: opp1 = 0,0,1,1,1  opp2 = 0,0,0,0,0 -> 3 war vs 7 peace -> peace
  //   last 5: opp1 = 1,1,1,0,0  opp2 = 1,1,0,0,0 -> 5 war vs 5 -> tie -> peace
  //   last 5: opp1 = 1,1,1,1,0  opp2 = 1,1,1,0,0 -> 6 war vs 4 -> war
  {
    auto a = Registry::instance().get("TolerantW5")();
    const bool ok =
        call(a.get(), {5, {0, 0, 0, 0, 0}, {0, 0, 1, 1, 1}, {0, 0, 0, 0, 0}}) == 0 &&
        call(a.get(), {5, {0, 0, 0, 0, 0}, {1, 1, 1, 0, 0}, {1, 1, 0, 0, 0}}) == 0 &&
        call(a.get(), {5, {0, 0, 0, 0, 0}, {1, 1, 1, 1, 0}, {1, 1, 1, 0, 0}}) == 1;
    report(ok, "TolerantW5: windowed last-5 war>peace");
  }

  // MajWindow3: war iff >=2 of last 3 rounds had an opponent at war.
  //   last3: rounds (o1,o2) = (1,0),(0,0),(0,1) -> 2 war-rounds -> war
  //         = (1,0),(0,0),(0,0) -> 1 -> peace
  {
    auto a = Registry::instance().get("MajWindow3")();
    const bool ok =
        call(a.get(), {3, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}}) == 1 &&
        call(a.get(), {3, {0, 0, 0}, {1, 0, 0}, {0, 0, 0}}) == 0 &&
        call(a.get(), {0, {}, {}, {}}) == 0;
    report(ok, "MajWindow3: majority war-rounds in last 3");
  }

  // TitFor2TatAvg: war if avg opp war-rate last 4 > 0.5.
  //   last4 (o1+o2 per round): 8 slots. (1,0),(1,0),(1,0),(1,0) -> 4/8=0.5 -> not >
  //   (1,1),(1,0),(0,0),(0,0) -> 3/8=0.375 -> peace
  //   (1,1),(1,1),(1,0),(0,0) -> 5/8=0.625 -> war
  {
    auto a = Registry::instance().get("TitFor2TatAvg")();
    const bool ok =
        call(a.get(), {4, {0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}}) == 0 &&  // 0.5, not >
        call(a.get(), {4, {0, 0, 0, 0}, {1, 1, 0, 0}, {1, 0, 0, 0}}) == 0 &&
        call(a.get(), {4, {0, 0, 0, 0}, {1, 1, 1, 0}, {1, 1, 0, 0}}) == 1;
    report(ok, "TitFor2TatAvg: avg opp war-rate > 0.5 over last 4");
  }

  // SuckerFirst: peace rds 0..9, then war.
  {
    auto a = Registry::instance().get("SuckerFirst")();
    const bool ok = call(a.get(), {9, {}, {}, {}}) == 0 &&
                    call(a.get(), {10, {}, {}, {}}) == 1 &&
                    call(a.get(), {15, {}, {}, {}}) == 1;
    report(ok, "SuckerFirst: peace rds 0..9 then war");
  }

  // Grim2Any: permanent after any opp's 2nd lifetime war.
  //   opp1 has exactly 1 war -> peace; opp1 has 2 -> war; opp2 has 2 -> war.
  {
    auto a = Registry::instance().get("Grim2Any")();
    const bool ok =
        call(a.get(), {3, {0, 0, 0}, {1, 0, 0}, {0, 0, 0}}) == 0 &&  // 1 war
        call(a.get(), {3, {0, 0, 0}, {1, 1, 0}, {0, 0, 0}}) == 1 &&  // opp1: 2 wars
        call(a.get(), {3, {0, 0, 0}, {0, 0, 0}, {1, 1, 0}}) == 1;    // opp2: 2 wars
    report(ok, "Grim2Any: permanent after an opp's 2nd war");
  }
}

void test_unit_stochastic() {
  std::printf("--- scripted-history unit tests (stochastic bots, seeded) ---\n");

  // T4TAgent: n==0 -> 0; n>=1 -> copy the rng-chosen opp's last move.
  //   bit = predict_bit(7777); expect (bit ? opp1 : opp2).
  {
    auto a = Registry::instance().get("T4TAgent")();
    const int bit = predict_bit(7777);
    mas_sim::fast_rng().reseed(7777);
    const int r0 = call(a.get(), {0, {}, {}, {}});
    mas_sim::fast_rng().reseed(7777);
    const int r1 = call(a.get(), {1, {0}, {0}, {1}});  // opp1=0, opp2=1
    const int expect1 = bit ? 0 : 1;
    report(r0 == 0 && r1 == expect1, "T4TAgent: opens peace, copies rng-chosen opp");
  }

  // RandomAgent: iid p=0.5; assert it matches the predicted bit for a seed.
  {
    auto a = Registry::instance().get("RandomAgent")();
    const int bit = predict_bit(7777);
    mas_sim::fast_rng().reseed(7777);
    const int r = call(a.get(), {0, {}, {}, {}});
    report(r == bit, "RandomAgent: matches predicted 0.5 bit (seed 7777)");
  }

  // NoisyP20: war iff uniform < 0.20. Use a seed whose uniform is known.
  {
    auto a = Registry::instance().get("NoisyP20")();
    const double u = predict_uniform(424242);
    mas_sim::fast_rng().reseed(424242);
    const int r = call(a.get(), {0, {}, {}, {}});
    const int expect = (u < 0.20) ? 1 : 0;
    report(r == expect, "NoisyP20: war iff uniform<0.20 (seed 424242)");
  }

  // NoisyP80: war iff uniform < 0.80.
  {
    auto a = Registry::instance().get("NoisyP80")();
    const double u = predict_uniform(424242);
    mas_sim::fast_rng().reseed(424242);
    const int r = call(a.get(), {0, {}, {}, {}});
    const int expect = (u < 0.80) ? 1 : 0;
    report(r == expect, "NoisyP80: war iff uniform<0.80 (seed 424242)");
  }

  // Joss10 at n=1 with both opps peace (TFT base = 0): flips to war iff
  // uniform < 0.10.
  {
    auto a = Registry::instance().get("Joss10")();
    const double u = predict_uniform(999999);
    mas_sim::fast_rng().reseed(999999);
    const int r = call(a.get(), {1, {0}, {0}, {0}});
    const int expect = (u < 0.10) ? 1 : 0;
    report(r == expect, "Joss10: TFT base 0, 10% flip-to-war (seed 999999)");
  }

  // Joss10 at n=1 with an opp warred (TFT base = 1): stays 1 regardless.
  {
    auto a = Registry::instance().get("Joss10")();
    mas_sim::fast_rng().reseed(999999);
    const int r = call(a.get(), {1, {0}, {1}, {0}});
    report(r == 1, "Joss10: TFT base 1 (opp warred) stays 1");
  }

  // GTFT_any_g30: repeated war (both opps warred last AND the round before)
  //   -> war, no RNG draw (deterministic).
  {
    auto a = Registry::instance().get("GTFT_any_g30")();
    mas_sim::fast_rng().reseed(123456);
    const int r = call(a.get(), {2, {0, 0}, {1, 1}, {1, 1}});  // both: WW
    report(r == 1, "GTFT_any_g30: repeated (non-isolated) war -> war");
  }

  // GTFT_any_g10: isolated opp1 war (warred last, peace before) -> maybe
  //   forgive with p=0.10.
  {
    auto a = Registry::instance().get("GTFT_any_g10")();
    const double u = predict_uniform(424242);
    mas_sim::fast_rng().reseed(424242);
    const int r = call(a.get(), {2, {0, 0}, {0, 1}, {0, 0}});  // opp1 isolated war
    const int expect = (u < 0.10) ? 0 : 1;
    report(r == expect, "GTFT_any_g10: isolated war, 10% forgive (seed 424242)");
  }

  // RandomWalk: n==0 -> 0; n<5 -> p = 0.5 (trend 0) so war iff uniform<0.5.
  {
    auto a = Registry::instance().get("RandomWalk")();
    const int r0 = call(a.get(), {0, {}, {}, {}});
    const double u = predict_uniform(424242);
    mas_sim::fast_rng().reseed(424242);
    const int r1 = call(a.get(), {1, {0}, {0}, {0}});  // one peace round
    const int expect1 = (u < 0.5) ? 1 : 0;
    report(r0 == 0 && r1 == expect1,
           "RandomWalk: opens peace; n<5 uses p=0.5");
  }
}

// ---------------------------------------------------------------------------
// Group (ii): smoke tournament.
// ---------------------------------------------------------------------------

bool finite_scores(const std::vector<double>& s) {
  if (s.empty()) return false;
  for (double v : s)
    if (!std::isfinite(v)) return false;
  return true;
}

void test_smoke_tournament() {
  std::printf("--- smoke tournament (all zoo bots, 5 runs, fast mode) ---\n");

  // Every registered zoo bot, one instance each. The two campaign builtins
  // (AlwaysPeace/AlwaysWar) are NOT linked into this test binary, so the
  // registry here contains exactly the 31 zoo names.
  const std::vector<std::string> names = Registry::instance().names();
  report(names.size() >= 30,
         (std::string("registered zoo bot count >= 30 (got ") +
          std::to_string(names.size()) + ")").c_str());

  std::vector<mas_sim::AgentFactory> factories;
  factories.reserve(names.size());
  for (const auto& n : names) factories.push_back(Registry::instance().get(n));
  const int N = static_cast<int>(names.size());

  // 5 runs, fast mode, distinct enumeration, seeded (CRN-friendly).
  bool all_finite = true;
  bool no_throw = true;
  std::vector<std::vector<double>> run_scores(5);
  for (int r = 0; r < 5; ++r) {
    try {
      run_scores[r] = run_tournament(factories, N, 1000 + r,
                                     TournamentMode::kDistinct,
                                     RngKind::kSfc64);
    } catch (const std::exception& e) {
      std::printf("      run %d threw: %s\n", r, e.what());
      no_throw = false;
      all_finite = false;
      continue;
    }
    if (!finite_scores(run_scores[r])) all_finite = false;
  }
  report(no_throw, "smoke tournament: no exceptions across 5 runs");
  report(all_finite, "smoke tournament: all scores finite");

  // Determinism: two runs with the same seed must be identical.
  {
    const auto a =
        run_tournament(factories, N, 777777, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    const auto b =
        run_tournament(factories, N, 777777, TournamentMode::kDistinct,
                       RngKind::kSfc64);
    report(a == b && finite_scores(a),
           "smoke tournament: same seed => identical score vector");
  }

  // Sanity (a): NiceAgent avg vs the saint-pair {NiceAgent, NiceAgent}
  //   vs NastyAgent's avg vs the same pair. Use a 3-bot match pool
  //   {subject, Nice, Nice} (distinct mode -> the single triple).
  {
    const auto nice_f = Registry::instance().get("NiceAgent");
    const auto nasty_f = Registry::instance().get("NastyAgent");

    auto nice_pool = [&nice_f] {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = nice_f; f[1] = nice_f; f[2] = nice_f;
      return f;
    };
    auto nasty_pool = [&nasty_f, &nice_f] {
      std::vector<mas_sim::AgentFactory> f(3);
      f[0] = nasty_f; f[1] = nice_f; f[2] = nice_f;
      return f;
    };

    double nice_avg = 0.0, nasty_avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      const auto sn = run_tournament(nice_pool(), 3, 2000 + r,
                                     TournamentMode::kDistinct,
                                     RngKind::kSfc64);
      const auto ss = run_tournament(nasty_pool(), 3, 2000 + r,
                                     TournamentMode::kDistinct,
                                     RngKind::kSfc64);
      nice_avg += sn[0];
      nasty_avg += ss[0];
    }
    nice_avg /= kRuns;
    nasty_avg /= kRuns;
    std::printf("      NiceAgent vs saint-pair = %.4f, NastyAgent = %.4f\n",
                nice_avg, nasty_avg);
    // In {Peace,Peace,Peace}: Nice slot 0 reads payoff[0][0][0]=4.
    // In {War,Peace,Peace}: Nasty slot 0 reads payoff[1][0][0]=6.
    // So the literal "Nice avg > Nasty avg vs saint-pair" is FALSE.
    // We assert the verifiable exact values and flag the direction.
    report(std::fabs(nice_avg - 4.0) < 1e-6 && std::fabs(nasty_avg - 6.0) < 1e-6,
           "saint-pair: Nice=4/rd, Nasty=6/rd (literal Nice>Nasty is FALSE)");
  }

  // Sanity (b): Grim_any "survives vs Nasty pairs better than NiceAgent".
  //   Pool {subject, Nasty, Nasty} (distinct). Grim_any turns war after the
  //   first Nasty war, so from round 1+ it is at war with both Nasty players:
  //   payoff[1][1][1] = 0/rd. Nice (always peace) vs two Nasty:
  //   payoff[0][1][1] = -1/rd. So Grim_any (0) > Nice (-1) -> survives better.
  {
    const auto grim_f = Registry::instance().get("Grim_any");
    const auto nice_f = Registry::instance().get("NiceAgent");
    const auto nasty_f = Registry::instance().get("NastyAgent");

    double grim_avg = 0.0, nice_avg = 0.0;
    constexpr int kRuns = 5;
    for (int r = 0; r < kRuns; ++r) {
      std::vector<mas_sim::AgentFactory> fg(3);
      fg[0] = grim_f; fg[1] = nasty_f; fg[2] = nasty_f;
      std::vector<mas_sim::AgentFactory> fn(3);
      fn[0] = nice_f; fn[1] = nasty_f; fn[2] = nasty_f;
      const auto sg = run_tournament(fg, 3, 3000 + r,
                                     TournamentMode::kDistinct,
                                     RngKind::kSfc64);
      const auto sn = run_tournament(fn, 3, 3000 + r,
                                     TournamentMode::kDistinct,
                                     RngKind::kSfc64);
      grim_avg += sg[0];
      nice_avg += sn[0];
    }
    grim_avg /= kRuns;
    nice_avg /= kRuns;
    std::printf("      Grim_any vs Nasty-pair = %.4f, NiceAgent = %.4f\n",
                grim_avg, nice_avg);
    report(grim_avg > nice_avg,
           "Grim_any survives vs Nasty-pair better than NiceAgent");
  }
}

}  // namespace

int main() {
  std::printf("=== zoo_t01_test ===\n");
  test_unit_deterministic();
  test_unit_stochastic();
  test_smoke_tournament();

  if (g_failures == 0) {
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("FAIL: %d/%d check(s) failed\n", g_failures, g_checks);
  return 1;
}
