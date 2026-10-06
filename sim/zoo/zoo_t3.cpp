// =============================================================================
// sim/zoo/zoo_t3.cpp — slice B3b: opponent zoo, Tier 3 (5 adversarial-elite
// bots). Single concern: the Tier-3 bots + their registration. No candidates,
// no analysis (later slices).
//
// Conventions mirrored exactly from zoo_t2.cpp / zoo_common.hpp:
//   * One class per bot + one make_agent_<name>() factory + one
//     REGISTER_AGENT(<name>); names are the literal spec tokens.
//   * Every stochastic bot draws ONLY from the engine's thread-local
//     fast_rng() via zoo_draw_uniform() (never ::rand(), never time-seeded);
//     the engine reseeds it from the run's seed, so a run is a pure function
//     of (pool, seed, mode, rng-kind).
//   * Constructors take parameters only (no RNG); every bot is O(1) or
//     O(window) per round (fixed probe windows / a 40-slot ring buffer).
//   * n==0 guards everywhere: no [n-1] indexing when n == 0.
//
// Round-index convention (flagged, same as zoo_t2): a bot decides the action
// for the round whose 0-based index is `n` (all three history vectors have
// length `n`). Spec tokens "rd k" are read as that 0-based index (consistent
// with the prior zoo slices' 0-based probe windows).
//
// Rule-ambiguity decisions (literal readings — flagged per dispatch):
//   * ZDExtort3P: "opens war" = war at n == 0. For n >= 1 the memory-1 tilt
//     draws p(peace) 0.20/0.55/0.85 for a last-round opp war-count of
//     0/1/2 (i.e. p(war) 0.80/0.45/0.15) from fast_rng() each round. Against
//     two pure-Nasty players the per-round ceiling is 0 (payoff[1][1][1]=0
//     for war, payoff[0][1][1]=-1 for peace), so the dispatch's "ZDExtort3P
//     vs {Nasty,Nasty} >= 1.5/rd" is unsatisfiable (same class of flag as
//     B2's Gradual3P vs Nasty-pair); the sanity test asserts the true bounds
//     and flags the literal direction.
//   * CounterProber: the dispatch says "classify ME from myHistory", but the
//     probe script P,P,W,W,P,W,P,P is a fixed, non-period-2, non-all-peace
//     sequence, so literal self-classification makes the period-2 lock and
//     saint-exploit branches unreachable (always TFT_any) and contradicts
//     the required "test all three classification branches". Operational
//     reading: classify each OPPONENT's rds 0..7 window (the only reading
//     under which the branches are reachable), OR-combining the two
//     per-opponent decisions (B2 CycleFitter / T4Tsmart precedent). The
//     lock's prediction is the period-2 continuation (round k ->
//     window[k % 2], k >= 8); war on a predicted-peace phase, peace on a
//     predicted-war phase.
//   * AdaptModel5: the 6 buckets are keyed on (my last action 0/1, opp
//     war-count last rd 0/1/2) and record my OBSERVED next-round war-rate
//     (a self-model). Each opponent's predicted next-round war prob is the
//     current-context bucket rate (both opps share one context, so they get
//     the same p; fallback 0.5 when that bucket has < 8 observations); the
//     joint opp-peace prob is (1-p)^2. The n==0 guard (no my last action)
//     uses context (0,0) with 0 obs -> p = 0.5 -> joint 0.25 < 0.45 -> war.
//   * ColludeSqueeze: literal; n == 0 -> war.
//   * MetaV1: "non-retaliators" = neither opp war over rds 0..13 (the
//     pre-commit window, mirroring B2 HybridA); "probe-retaliators" = at
//     least one opp war there. "Sustained-squeeze" = my last 5 COMPLETED
//     rounds were all war and none of them had BOTH opponents war (a double
//     retaliation) -> commit to permanent war (monotone).
// =============================================================================

#include "zoo_common.hpp"

namespace mas_sim {

// =============================================================================
// Shared small helpers (O(window)) — local to this TU.
// =============================================================================
namespace {

// Count 1s (war) in the index range [lo, hi) of h (clamped to its length).
inline int war_in_range(const std::vector<int>& h, int lo, int hi) {
  int c = 0;
  const int n = static_cast<int>(h.size());
  if (lo < 0) lo = 0;
  if (hi > n) hi = n;
  for (int i = lo; i < hi; ++i) c += h[i];
  return c;
}

// CounterProber's per-opponent probe-window classification over rds 0..7
// (the window is fixed, so it is re-derived from history each round and the
// bot is stateless). Returns the decision for round k (k >= 8):
//   * exact period-2 window -> exploitation lock: war on the predicted-
//     peace phase, peace on the predicted-war phase (period-2 continuation:
//     round k -> window[k % 2]);
//   * all-peace window      -> permanent war (saint-exploit);
//   * otherwise             -> TFT_any (mirror that opponent's last move).
int cp_classify(const std::vector<int>& o, int k) {
  int m[8];
  for (int i = 0; i < 8; ++i) m[i] = o[i];
  bool p2 = true;
  for (int i = 0; i < 6; ++i) {
    if (m[i] != m[i + 2]) {
      p2 = false;
      break;
    }
  }
  if (p2) return (m[k % 2] == 0) ? 1 : 0;  // lock: invert the prediction
  int ws = 0;
  for (int i = 0; i < 8; ++i) ws += m[i];
  if (ws == 0) return 1;  // saint-exploit: permanent war
  return (o[k - 1] == 1) ? 1 : 0;  // TFT_any
}

}  // namespace

// =============================================================================
// Tier 3 — 5 adversarial-elite bots.
// =============================================================================

// 1. ZDExtort3P: opens war; then p(peace) = 0.20/0.55/0.85 for a last-round
//    opp war-count of 0/1/2 (stochastic; draw from fast_rng() each round).
class ZDExtort3P : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 1;  // open war
    const int c = (o1[n - 1] == 1) + (o2[n - 1] == 1);  // 0 / 1 / 2
    const double p_peace = (c == 0) ? 0.20 : (c == 1) ? 0.55 : 0.85;
    return (zoo_draw_uniform() < p_peace) ? 0 : 1;
  }
};

// 2. CounterProber: scripted probe P,P,W,W,P,W,P,P on rds 0..7; then (n >= 8)
//    per-opponent probe-window classification (cp_classify), OR-combined
//    across the two opponents.
class CounterProber : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    static const int kScript[8] = {0, 0, 1, 1, 0, 1, 0, 0};
    if (n < 8) return kScript[n];  // n==0 guard: scripted, no indexing
    return (cp_classify(o1, n) == 1 || cp_classify(o2, n) == 1) ? 1 : 0;
  }
};

// 3. AdaptModel5: trailing-40 self-model over the 6 contexts (my last
//    action 0/1, opp war-count last rd 0/1/2). Each opponent's predicted
//    next-round war prob = current-context bucket rate (fallback 0.5 when <
//    8 obs); war iff the joint opp-peace prob (1-p)^2 < 0.45.
class AdaptModel5 : public AgentBase {
  struct Obs {
    unsigned char m;  // my action in the context round (i-1)
    unsigned char c;  // opp war-count in the context round (0..2)
    unsigned char v;  // my action in the round that followed the context
  };
  Obs ring_[40] = {};
  int head_ = 0;
  int count_ = 0;
  int processed_ = 0;  // last ingested observation index i (i >= 1)

 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    // Ingest newly completed observations (round i uses context i-1).
    for (int i = processed_ + 1; i <= n - 1; ++i) {
      if (i < 1) continue;
      ring_[head_] = Obs{static_cast<unsigned char>(my[i - 1]),
                         static_cast<unsigned char>(o1[i - 1] + o2[i - 1]),
                         static_cast<unsigned char>(my[i])};
      head_ = (head_ + 1) % 40;
      if (count_ < 40) ++count_;
      processed_ = i;
    }
    int m0, c0;
    if (n == 0) {
      m0 = 0;  // n==0 guard: no my last action -> 0
      c0 = 0;
    } else {
      m0 = my[n - 1];
      c0 = o1[n - 1] + o2[n - 1];
    }
    // Recompute the 6 buckets from the trailing-40 ring (O(window)).
    int cnt[2][3] = {};
    int war[2][3] = {};
    for (int j = 0; j < count_; ++j) {
      const Obs& o = ring_[(head_ - count_ + j + 40) % 40];
      ++cnt[o.m][o.c];
      war[o.m][o.c] += o.v;
    }
    const int cc = cnt[m0][c0];
    const double p = (cc >= 8)
        ? static_cast<double>(war[m0][c0]) / static_cast<double>(cc)
        : 0.5;
    const double joint_peace = (1.0 - p) * (1.0 - p);
    return (joint_peace < 0.45) ? 1 : 0;
  }
};

// 4. ColludeSqueeze: war iff BOTH opponents peace last rd; peace otherwise;
//    n == 0 -> war.
class ColludeSqueeze : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 1;
    return (o1[n - 1] == 0 && o2[n - 1] == 0) ? 1 : 0;
  }
};

// 5. MetaV1: HybridA-style two-test probe (peace rds 0..4; W probe at rd 5;
//    gap rds 6..10; burst W rds 11..13 at rate 0.6). Then (n >= 14):
//    non-retaliators (no opp war rds 0..13) -> farming at rate 0.35;
//    probe-retaliators -> TFT_any. Sustained-squeeze: once my last 5
//    completed rounds were all war and none of them had a BOTH-opp
//    retaliation -> commit to permanent war (monotone).
class MetaV1 : public AgentBase {
  bool committed_ = false;

 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (committed_) return 1;  // committed: permanent war
    if (n >= 5) {
      // Sustained-squeeze over the last 5 COMPLETED rounds.
      bool my_all_war = true;
      bool both_any = false;
      for (int i = n - 5; i <= n - 1; ++i) {
        if (my[i] != 1) my_all_war = false;
        if (o1[i] == 1 && o2[i] == 1) both_any = true;
      }
      if (my_all_war && !both_any) {
        committed_ = true;
        return 1;
      }
    }
    int a;
    if (n < 5) {
      a = 0;  // pre-probe silence
    } else if (n == 5) {
      a = 1;  // probe
    } else if (n <= 10) {
      a = 0;  // gap
    } else if (n <= 13) {
      a = (zoo_draw_uniform() < 0.6) ? 1 : 0;  // burst
    } else {
      const int pre = war_in_range(o1, 0, 14) + war_in_range(o2, 0, 14);
      if (pre == 0) {
        a = (zoo_draw_uniform() < 0.35) ? 1 : 0;  // non-retaliators: farm
      } else {
        a = (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;  // probe-retaliators
      }
    }
    return a;
  }
};

// =============================================================================
// Factories — one per registered name (name == the literal spec token).
// =============================================================================

AgentFactory make_agent_ZDExtort3P() {
  return [] { return std::make_unique<ZDExtort3P>(); };
}
AgentFactory make_agent_CounterProber() {
  return [] { return std::make_unique<CounterProber>(); };
}
AgentFactory make_agent_AdaptModel5() {
  return [] { return std::make_unique<AdaptModel5>(); };
}
AgentFactory make_agent_ColludeSqueeze() {
  return [] { return std::make_unique<ColludeSqueeze>(); };
}
AgentFactory make_agent_MetaV1() {
  return [] { return std::make_unique<MetaV1>(); };
}

// Registration (names are the literal spec tokens).
REGISTER_AGENT(ZDExtort3P);
REGISTER_AGENT(CounterProber);
REGISTER_AGENT(AdaptModel5);
REGISTER_AGENT(ColludeSqueeze);
REGISTER_AGENT(MetaV1);

}  // namespace mas_sim
