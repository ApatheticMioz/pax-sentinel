// =============================================================================
// sim/zoo/zoo_t01.cpp — slice B2: opponent zoo, Tiers 0 + 1
//
// Single concern: the zoo bots + their registration. No candidates, no analysis.
//
// TIER 0 — the six provided samples, behaviors VERBATIM to
// ThreeWayPeaceWar.cpp / sim/fidelity/sample_fixture.cpp, but with all RNG
// routed through the engine-provided Rng (never ::rand(), never time-seeded)
// so each run is deterministic:
//   NiceAgent     always 0
//   NastyAgent    always 1
//   RandomAgent   war with p = 0.5  (sample: rand() < RAND_MAX/2 ? 0 : 1)
//   TolerantAgent war iff cumulative war > peace across BOTH opps' full history
//   FreakyAgent   one constant action per match, drawn in the constructor
//   T4TAgent      n==0 -> 0 else copy the rng-chosen opponent's last move
//
// TIER 1 — naive student variants, all O(1)/O(window) per round except where
// the rule IS a stateless full-history rescan (Grim_any, Grim_maj, Grim2Any,
// TolerantAgent).
//
// RNG contract: every stochastic bot draws ONLY from fast_rng() (the
// thread-local sfc64 handle) via zoo_draw_bit() / zoo_draw_uniform(). The
// engine reseeds fast_rng() from the run's seed at the start of each
// tournament, so a given (pool, seed) is a pure function of that stream.
// Constructors take parameters only (no RNG) EXCEPT the sample-faithful
// FreakyAgent / FreakyP, whose per-match constant IS drawn in the constructor
// (preserving the sample's "constructor draw precedes the horizon draw"
// ordering).
// =============================================================================

#include "zoo_common.hpp"

namespace mas_sim {

// =============================================================================
// Shared small helpers (O(window))
// =============================================================================
namespace {

// Count 1s (war) in the last `w` entries of h (fewer if h is shorter).
inline int war_count_last(const std::vector<int>& h, int w) {
  int c = 0;
  const int n = static_cast<int>(h.size());
  int lo = n - w;
  if (lo < 0) lo = 0;
  for (int i = lo; i < n; ++i) c += h[i];
  return c;
}

}  // namespace

// =============================================================================
// TIER 0 — provided samples (RNG routed through the engine)
// =============================================================================

class NiceAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return 0;
  }
};

class NastyAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return 1;
  }
};

class RandomAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    // Sample: rand() < RAND_MAX/2 ? 0 : 1  (p(war) = 0.5).
    return zoo_draw_bit();
  }
};

class TolerantAgent : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    int peace = 0, war = 0;
    for (int i = 0; i < n; ++i) (opp1[i] == 0) ? ++peace : ++war;
    for (int i = 0; i < n; ++i) (opp2[i] == 0) ? ++peace : ++war;
    return war > peace ? 1 : 0;
  }
};

class FreakyAgent : public AgentBase {
  int action_;
 public:
  // The sample draws the constant in the constructor (before the horizon
  // draw). We draw from the engine RNG here — same ordering, engine-provided.
  FreakyAgent() { action_ = zoo_draw_bit(); }

  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return action_;
  }
};

class T4TAgent : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;  // peace by default
    // Sample: rand() < RAND_MAX/2 ? opp1[n-1] : opp2[n-1].
    return zoo_draw_bit() ? opp1[n - 1] : opp2[n - 1];
  }
};

// =============================================================================
// TIER 1
// =============================================================================

// 1. TFT_any: war iff EITHER opponent warred last round; opens peace.
class TFTAny : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;
    return (opp1[n - 1] == 1 || opp2[n - 1] == 1) ? 1 : 0;
  }
};

// 2. TFT_maj: war iff BOTH opponents warred last round; opens peace.
class TFTMaj : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;
    return (opp1[n - 1] == 1 && opp2[n - 1] == 1) ? 1 : 0;
  }
};

// 3. TF2T_any: war iff EITHER opponent warred on both of the last 2 rds.
class TF2TAny : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n < 2) return 0;
    const bool w1 = opp1[n - 1] == 1 && opp1[n - 2] == 1;
    const bool w2 = opp2[n - 1] == 1 && opp2[n - 2] == 1;
    return (w1 || w2) ? 1 : 0;
  }
};

// 4. TF2T_maj: war iff BOTH opponents warred on both of the last 2 rds.
class TF2TMaj : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n < 2) return 0;
    const bool w1 = opp1[n - 1] == 1 && opp1[n - 2] == 1;
    const bool w2 = opp2[n - 1] == 1 && opp2[n - 2] == 1;
    return (w1 && w2) ? 1 : 0;
  }
};

// 5. Grim_any: permanent war after the first war by either opponent.
//    Stateless rescan (the rule IS "has any war ever occurred").
class GrimAny : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    for (int i = 0; i < n; ++i)
      if (opp1[i] == 1 || opp2[i] == 1) return 1;
    return 0;
  }
};

// 6. Grim_maj: permanent war after the first round where BOTH warred.
//    Stateless rescan.
class GrimMaj : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    for (int i = 0; i < n; ++i)
      if (opp1[i] == 1 && opp2[i] == 1) return 1;
    return 0;
  }
};

// 7/8. GTFT_any_g10 / GTFT_any_g30: TFT_any, but forgive an ISOLATED
//      opponent war (warred last round but peace the round before — a lone
//      defection, not a run) with probability g. A repeated war (also
//      warred the round before) is never forgiven.
class GTFTAny : public AgentBase {
  double g_;
 public:
  explicit GTFTAny(double g) : g_(g) {}

  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;
    bool any = false;
    bool all_isolated = true;
    if (opp1[n - 1] == 1) {
      any = true;
      if (n >= 2 && opp1[n - 2] == 1) all_isolated = false;
    }
    if (opp2[n - 1] == 1) {
      any = true;
      if (n >= 2 && opp2[n - 2] == 1) all_isolated = false;
    }
    if (!any) return 0;
    if (!all_isolated) return 1;  // repeated war -> war
    return (zoo_draw_uniform() < g_) ? 0 : 1;  // isolated -> maybe forgive
  }
};

// 9. WSLS_naive: repeat my last action iff my last-round payoff >= 2, else
//    switch it; opens peace. Own payoff computed from the histories.
class WSLSNaive : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;
    const int prev = my[n - 1];
    const int p = kZooPayoff[prev][opp1[n - 1]][opp2[n - 1]];
    return (p >= 2) ? prev : (1 - prev);
  }
};

// 10. Alternator: P, W, P, W, ...
class Alternator : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return (n % 2) == 0 ? 0 : 1;
  }
};

// 11. Periodic PPW: peace, peace, war, peace, peace, war, ...
class PeriodicPPW : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return (n % 3) == 2 ? 1 : 0;
  }
};

// 12/13. Freaky25 / Freaky75: one constant per match (war w.p. p).
class FreakyP : public AgentBase {
  int action_;
  double p_;
 public:
  explicit FreakyP(double p) : p_(p) {
    // Per-match constant: draw once in the constructor (engine RNG), as in
    // the sample's FreakyAgent.
    action_ = (zoo_draw_uniform() < p_) ? 1 : 0;
  }

  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return action_;
  }
};

// 14. BrokenTFT_1: reads ONLY opponent 1's last move (war iff it warred);
//     ignores opponent 2; opens peace.
class BrokenTFT1 : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>&) override {
    if (n == 0) return 0;
    return opp1[n - 1];
  }
};

// 15. Joss10: TFT_any, then flip the resulting decision to war with p=0.10
//     (idempotent if already war).
class Joss10 : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    int a;
    if (n == 0) a = 0;
    else a = (opp1[n - 1] == 1 || opp2[n - 1] == 1) ? 1 : 0;
    if (a == 0 && zoo_draw_uniform() < 0.10) a = 1;
    return a;
  }
};

// 16. Detective: probe W,P,P,P; if BOTH opponents are all-peace over the
//     probe window -> permanent war (a confirmed saint is not a threat);
//     otherwise TFT_any.
class Detective : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n < 1) {
      // Pre-probe: probe War on round 0.
      return 1;
    }
    if (n <= 4) {
      // In-probe (rounds 1..3): probe Peace.
      return 0;
    }
    // Post-probe (n >= 5): if both opponents stayed all-peace through the
    // probe window (rds 0..3) they are saints -> permanent war; else TFT_any.
    if (war_count_last(opp1, 4) == 0 && war_count_last(opp2, 4) == 0)
      return 1;
    return (opp1[n - 1] == 1 || opp2[n - 1] == 1) ? 1 : 0;
  }
};

// 17/19. NoisyP20 / NoisyP80: iid war with the given p.
// (NoisyP50 is a behavioral duplicate of the tier-0 RandomAgent — both are
//  iid p=0.5 — so per the dispatch's "skip if dup" note it is NOT registered;
//  the NoisyP class is still the shared implementation for 20 and 80.)
class NoisyP : public AgentBase {
  double p_;
 public:
  explicit NoisyP(double p) : p_(p) {}
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return (zoo_draw_uniform() < p_) ? 1 : 0;
  }
};

// 20/21. TolerantW5 / TolerantW10: war iff war>peace over BOTH opponents'
//        last-W rounds (windowed TolerantAgent).
class TolerantW : public AgentBase {
  int w_;
 public:
  explicit TolerantW(int w) : w_(w) {}
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;
    int lo = n - w_;
    if (lo < 0) lo = 0;
    int peace = 0, war = 0;
    for (int i = lo; i < n; ++i) (opp1[i] == 0) ? ++peace : ++war;
    for (int i = lo; i < n; ++i) (opp2[i] == 0) ? ++peace : ++war;
    return war > peace ? 1 : 0;
  }
};

// 22. PavlovMaj: my last-round payoff >= 2 -> repeat my last action else
//     switch it; opens peace. (The "majority-opponent last move as context"
//     reading reduces to the same payoff-based switch as WSLS in this
//     tensor, since both opponents have a fixed last move; the payoff rule
//     is the operative part and is identical to WSLS_naive's decision.)
class PavlovMaj : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;
    const int prev = my[n - 1];
    const int p = kZooPayoff[prev][opp1[n - 1]][opp2[n - 1]];
    return (p >= 2) ? prev : (1 - prev);
  }
};

// 23. RandomWalk: war prob = 0.5 + 0.4 * trend / 8, where trend =
//     (sum of my last-5 payoffs) - 4*cnt (deviation from the all-peace
//     baseline of 4/rd). Clamped to [0,1]. Naive momentum: doing well
//     (payoff > 4) → more likely to continue war; doing poorly → less.
class RandomWalk : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;
    int sum = 0, cnt = 0;
    for (int i = n - 1; i >= 0 && cnt < 5; --i, ++cnt)
      sum += kZooPayoff[my[i]][opp1[i]][opp2[i]];
    const int trend = sum - 4 * cnt;  // deviation from 4/rd baseline
    double p = 0.5 + 0.4 * (static_cast<double>(trend) / 8.0);
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    return (zoo_draw_uniform() < p) ? 1 : 0;
  }
};

// 24. Grim2Any: permanent war after any opponent's 2nd lifetime war.
//    Stateless rescan.
class Grim2Any : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    int c1 = 0, c2 = 0;
    for (int i = 0; i < n; ++i) {
      c1 += opp1[i];
      c2 += opp2[i];
    }
    return (c1 >= 2 || c2 >= 2) ? 1 : 0;
  }
};

// 25. GrimAnyForgive1: Grim_any, but reset to peace if BOTH opponents have
//     been peace for the last 10 consecutive rounds.
class GrimAnyForgive1 : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n >= 10 && war_count_last(opp1, 10) == 0 &&
        war_count_last(opp2, 10) == 0)
      return 0;  // both calm for 10 rds -> forgive
    for (int i = 0; i < n; ++i)
      if (opp1[i] == 1 || opp2[i] == 1) return 1;
    return 0;
  }
};

// 26. MajWindow3: war iff a majority of (opp1, opp2) warred in a majority of
//     the last 3 rounds (i.e. >= 2 of the last <=3 rounds had at least one
//     opponent at war).
class MajWindow3 : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n < 1) return 0;
    int lo = n - 3;
    if (lo < 0) lo = 0;
    int rounds_with_war = 0;
    for (int i = lo; i < n; ++i)
      if (opp1[i] == 1 || opp2[i] == 1) ++rounds_with_war;
    return (rounds_with_war >= 2) ? 1 : 0;
  }
};

// 27. TitFor2TatAvg: war iff the average opponent war-rate over the last 4
//     rounds > 0.5 (i.e. more than half of all opponent-actions were war).
class TitFor2TatAvg : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n < 1) return 0;
    int lo = n - 4;
    if (lo < 0) lo = 0;
    int w = 0, total = 0;
    for (int i = lo; i < n; ++i) {
      w += opp1[i] + opp2[i];
      total += 2;
    }
    return (2 * w > total) ? 1 : 0;
  }
};

// 28. SuckerFirst: peace for rounds 0..9, then all war.
class SuckerFirst : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return (n >= 10) ? 1 : 0;
  }
};

// =============================================================================
// Factories — one per registered name (name == the literal spec token).
// =============================================================================

// Tier 0
AgentFactory make_agent_NiceAgent() {
  return [] { return std::make_unique<NiceAgent>(); };
}
AgentFactory make_agent_NastyAgent() {
  return [] { return std::make_unique<NastyAgent>(); };
}
AgentFactory make_agent_RandomAgent() {
  return [] { return std::make_unique<RandomAgent>(); };
}
AgentFactory make_agent_TolerantAgent() {
  return [] { return std::make_unique<TolerantAgent>(); };
}
AgentFactory make_agent_FreakyAgent() {
  return [] { return std::make_unique<FreakyAgent>(); };
}
AgentFactory make_agent_T4TAgent() {
  return [] { return std::make_unique<T4TAgent>(); };
}

// Tier 1
AgentFactory make_agent_TFT_any() {
  return [] { return std::make_unique<TFTAny>(); };
}
AgentFactory make_agent_TFT_maj() {
  return [] { return std::make_unique<TFTMaj>(); };
}
AgentFactory make_agent_TF2T_any() {
  return [] { return std::make_unique<TF2TAny>(); };
}
AgentFactory make_agent_TF2T_maj() {
  return [] { return std::make_unique<TF2TMaj>(); };
}
AgentFactory make_agent_Grim_any() {
  return [] { return std::make_unique<GrimAny>(); };
}
AgentFactory make_agent_Grim_maj() {
  return [] { return std::make_unique<GrimMaj>(); };
}
AgentFactory make_agent_GTFT_any_g10() {
  return [] { return std::make_unique<GTFTAny>(0.10); };
}
AgentFactory make_agent_GTFT_any_g30() {
  return [] { return std::make_unique<GTFTAny>(0.30); };
}
AgentFactory make_agent_WSLS_naive() {
  return [] { return std::make_unique<WSLSNaive>(); };
}
AgentFactory make_agent_Alternator() {
  return [] { return std::make_unique<Alternator>(); };
}
AgentFactory make_agent_PeriodicPPW() {
  return [] { return std::make_unique<PeriodicPPW>(); };
}
AgentFactory make_agent_Freaky25() {
  return [] { return std::make_unique<FreakyP>(0.25); };
}
AgentFactory make_agent_Freaky75() {
  return [] { return std::make_unique<FreakyP>(0.75); };
}
AgentFactory make_agent_BrokenTFT_1() {
  return [] { return std::make_unique<BrokenTFT1>(); };
}
AgentFactory make_agent_Joss10() {
  return [] { return std::make_unique<Joss10>(); };
}
AgentFactory make_agent_Detective() {
  return [] { return std::make_unique<Detective>(); };
}
AgentFactory make_agent_NoisyP20() {
  return [] { return std::make_unique<NoisyP>(0.20); };
}
AgentFactory make_agent_NoisyP80() {
  return [] { return std::make_unique<NoisyP>(0.80); };
}
AgentFactory make_agent_TolerantW5() {
  return [] { return std::make_unique<TolerantW>(5); };
}
AgentFactory make_agent_TolerantW10() {
  return [] { return std::make_unique<TolerantW>(10); };
}
AgentFactory make_agent_PavlovMaj() {
  return [] { return std::make_unique<PavlovMaj>(); };
}
AgentFactory make_agent_RandomWalk() {
  return [] { return std::make_unique<RandomWalk>(); };
}
AgentFactory make_agent_Grim2Any() {
  return [] { return std::make_unique<Grim2Any>(); };
}
AgentFactory make_agent_GrimAnyForgive1() {
  return [] { return std::make_unique<GrimAnyForgive1>(); };
}
AgentFactory make_agent_MajWindow3() {
  return [] { return std::make_unique<MajWindow3>(); };
}
AgentFactory make_agent_TitFor2TatAvg() {
  return [] { return std::make_unique<TitFor2TatAvg>(); };
}
AgentFactory make_agent_SuckerFirst() {
  return [] { return std::make_unique<SuckerFirst>(); };
}

// Registration (names are the literal spec tokens; NoisyP50 intentionally
// omitted as a behavioral duplicate of RandomAgent — see the note above).
REGISTER_AGENT(NiceAgent);
REGISTER_AGENT(NastyAgent);
REGISTER_AGENT(RandomAgent);
REGISTER_AGENT(TolerantAgent);
REGISTER_AGENT(FreakyAgent);
REGISTER_AGENT(T4TAgent);

REGISTER_AGENT(TFT_any);
REGISTER_AGENT(TFT_maj);
REGISTER_AGENT(TF2T_any);
REGISTER_AGENT(TF2T_maj);
REGISTER_AGENT(Grim_any);
REGISTER_AGENT(Grim_maj);
REGISTER_AGENT(GTFT_any_g10);
REGISTER_AGENT(GTFT_any_g30);
REGISTER_AGENT(WSLS_naive);
REGISTER_AGENT(Alternator);
REGISTER_AGENT(PeriodicPPW);
REGISTER_AGENT(Freaky25);
REGISTER_AGENT(Freaky75);
REGISTER_AGENT(BrokenTFT_1);
REGISTER_AGENT(Joss10);
REGISTER_AGENT(Detective);
REGISTER_AGENT(NoisyP20);
REGISTER_AGENT(NoisyP80);
REGISTER_AGENT(TolerantW5);
REGISTER_AGENT(TolerantW10);
REGISTER_AGENT(PavlovMaj);
REGISTER_AGENT(RandomWalk);
REGISTER_AGENT(Grim2Any);
REGISTER_AGENT(GrimAnyForgive1);
REGISTER_AGENT(MajWindow3);
REGISTER_AGENT(TitFor2TatAvg);
REGISTER_AGENT(SuckerFirst);

}  // namespace mas_sim
