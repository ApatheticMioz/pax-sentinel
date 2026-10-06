// =============================================================================
// sim/zoo/zoo_t2.cpp — slice B3a: opponent zoo, Tier 2 (16 "strong-submitter"
// bots). Single concern: the Tier-2 bots + their registration. No Tier-3, no
// candidates, no analysis (later slices).
//
// Conventions mirrored from zoo_t01.cpp / zoo_common.hpp:
//   * One class per bot + one `make_agent_<name>()` factory + one
//     REGISTER_AGENT(<name>) per name; names are the literal spec tokens.
//   * Every stochastic bot draws ONLY from the engine's thread-local
//     fast_rng() via zoo_draw_bit()/zoo_draw_uniform() (never ::rand(), never
//     time-seeded); the engine reseeds fast_rng() from the run's seed, so a
//     run is a pure function of (pool, seed, mode, rng-kind).
//   * Constructors take parameters only (no RNG); every bot is O(1) or
//     O(window) per round. Stateful bots (the "3P" / learner / exploit
//     families) keep match-local member state; the engine constructs a fresh
//     agent per match and calls selectAction once per round in round order,
//     so incremental member state is valid.
//
// Round-index convention (flagged): a bot decides the action for the round
// whose 0-based index is `n` (the number of completed rounds; all three
// history vectors have length `n`). Spec tokens "rd k" are read as that
// 0-based index (consistent with zoo_t01's 0-based probe windows: "rds
// 0..3", "rds 0..14").
//
// Rule-ambiguity decisions (literal readings — flagged per dispatch):
//   * Prober_DDCC: probe window is the first 4 rounds (rds 0..3). "Either opp
//     warred >=2 in probe" is checked over that same 4-round window; the
//     "else -> farming" branch is the remainder (neither all-peace nor >=2).
//   * SaintHunter: "commitment" = zero opponent wars over rds 0..14. If some
//     opponent DID war in that window (no commitment), the literal spec is
//     silent, so the operative continuation is TFT_any (default). Once
//     committed to war, any opponent war after rd 14 reverts to TFT_any
//     (monotone, so effectively permanent).
//   * CTFT3P: the "contrite" override (unprovoked self-war last round) takes
//     precedence over the TFT_any base and yields peace this round.
//   * Omega3P: feud window is the last 6 rounds (needs n>=6); amnesty length
//     is 2 +/- 1 (i.e. 1..3) drawn from fast_rng().
//   * KLenient2/3: "trigger" = an opponent with >=T wars in the last W
//     rounds (T,W = 2,3 for KLenient2 and 3,4 for KLenient3). "Forgive an
//     isolated war w.p. 0.2" is read as: any sub-threshold war activity
//     (1 <= opp wars < T in the window) is treated as a trigger with
//     probability 0.8 (forgiven, i.e. no trigger, with p 0.2). A sub-
//     threshold count of 0 never triggers.
//   * EndgameGTFT: "one-shot forgiveness" = GTFT-style — an isolated opponent
//     war (warred last, peace before; no double-defection) is forgiven (peace),
//     a repeated war is war. "rd t" is read as the 0-based index n; the
//     deterministic ramp p=(n-94)/12 is clamped to 1 so t>=107 is always war
//     (the formula already reaches 1 at n=106). Final action = GTFT decision
//     OR the ramp draw.
//   * HybridA: before the probe (n<5) and between probe and burst (6..10) the
//     spec is silent -> peace. "Retaliators" = any opponent that warred at
//     least once over rds 0..13 (the pre-commit window); "silent
//     non-retaliators" = neither opponent ever warred there.
//   * HybridB: "retaliators" = any opponent that warred over rds 0..8
//     (pre/probe window); non-retaliators -> farming at rate 0.50.
//   * CycleFitter: an opponent is "periodic" if its last 8 moves satisfy an
//     exact period-2 or period-3 (checked period-2 first). Its next move is
//     predicted from the 8-window; the bot plays the inverse (war on a
//     predicted-peace phase, peace on a predicted-war phase). A non-periodic
//     opponent is handled per TFT_any (mirror its last move). The single
//     emitted action is 1 if either opponent warrants war (OR).
//   * T4Tsmart: "5% forgiveness flip after own retaliation" = after a round
//     where I warred, if the base decision is war, flip to peace with p 0.05.
// =============================================================================

#include "zoo_common.hpp"

namespace mas_sim {

// =============================================================================
// Shared small helpers (O(window)) — local to this TU.
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

// Count 1s (war) in the inclusive/exclusive index range [lo, hi) of h.
inline int war_in_range(const std::vector<int>& h, int lo, int hi) {
  int c = 0;
  const int n = static_cast<int>(h.size());
  if (lo < 0) lo = 0;
  if (hi > n) hi = n;
  for (int i = lo; i < hi; ++i) c += h[i];
  return c;
}

// Map a single fast_rng() draw to a jitter in {-1, 0, +1} (three equal
// thirds of the 15-bit range).
inline int draw_jitter() {
  const int r = fast_rng().rand();  // [0, 32767]
  return (r < 10923) ? -1 : (r < 21845) ? 0 : 1;
}

}  // namespace

// =============================================================================
// Tier 2 — 16 strong-submitter bots.
// =============================================================================

// 1. Prober_DDCC: probe P,P,W,P on rds 0..3. Then (n>=4):
//   * both opps all-peace through the probe  -> permanent war;
//   * either opp warred >=2 in the probe     -> TFT_any;
//   * otherwise                              -> farming at war-rate 0.35
//     (own-rng, non-periodic: draw per round).
class ProberDDCC : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;          // rd 0: P
    if (n == 1) return 0;          // rd 1: P
    if (n == 2) return 1;          // rd 2: W (probe)
    if (n == 3) return 0;          // rd 3: P
    // n >= 4: post-probe classification over the first 4 rounds.
    if (war_in_range(o1, 0, 4) == 0 && war_in_range(o2, 0, 4) == 0)
      return 1;  // both saints -> permanent war
    if (war_in_range(o1, 0, 4) >= 2 || war_in_range(o2, 0, 4) >= 2)
      return (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;  // TFT_any
    return (zoo_draw_uniform() < 0.35) ? 1 : 0;  // farming, non-periodic
  }
};

// 2. SaintHunter: peace on rds 0..14. Zero opponent wars over that window ->
//    commit to permanent war; any opponent war after the commitment -> revert
//    to TFT_any (monotone => effectively permanent). If some opponent DID war
//    in the window (no commitment) -> TFT_any (literal gap; flagged).
class SaintHunter : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n < 15) return 0;  // rds 0..14: peace
    const int tft = (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;
    const int window_wars =
        war_in_range(o1, 0, 15) + war_in_range(o2, 0, 15);  // rds 0..14
    if (window_wars == 0) {
      // Committed to permanent war, unless an opp warred after the commit.
      const int after = war_in_range(o1, 15, n) + war_in_range(o2, 15, n);
      return (after >= 1) ? tft : 1;
    }
    return tft;  // no clean commitment -> TFT_any
  }
};

// 3. Gradual3P: opens peace. Each new provocation (any opp warred last round
//    while not already punishing) -> war `prov` rounds, then 2 peace amnesty
//    rounds; prov increments per event (1, 2, 3, ...).
class Gradual3P : public AgentBase {
  int prov_ = 0;    // provocation_count (war rounds for the next event)
  int phase_ = 0;   // 0 = normal, 1 = punishing, 2 = amnesty
  int rem_ = 0;     // rounds left in the current phase
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (phase_ == 1) {  // punishing
      if (rem_ > 0) {
        --rem_;
        return 1;
      }
      phase_ = 2;
      rem_ = 2;  // 2 peace amnesty rounds
      return 0;
    }
    if (phase_ == 2) {  // amnesty
      --rem_;
      if (rem_ <= 0) phase_ = 0;
      return 0;
    }
    // normal
    if (n >= 1 && (o1[n - 1] == 1 || o2[n - 1] == 1)) {
      ++prov_;        // provocation_count++ per event
      phase_ = 1;
      rem_ = prov_ - 1;  // remaining punish rounds after this one
      return 1;        // first punish war round
    }
    return 0;
  }
};

// 4. CTFT3P: TFT_any, but after a round where I warred while both opps were
//    peace (unprovoked) -> this round is contrite peace (skip the retaliation).
class CTFT3P : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;
    const int base = (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;
    if (my[n - 1] == 1 && o1[n - 1] == 0 && o2[n - 1] == 0) return 0;  // contrite
    return base;
  }
};

// 5. Omega3P: TFT_any + feud detector. In the last 6 rounds (n>=6), if >=4
//    rounds had >=1 opp war AND I warred >=4 of those 6 -> play 2 peace
//    (amnesty, jitter ±1 -> 1..3) then resume.
class Omega3P : public AgentBase {
  int phase_ = 0;  // 0 = normal, 2 = amnesty
  int rem_ = 0;
 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (phase_ == 2) {
      --rem_;
      if (rem_ <= 0) phase_ = 0;
      return 0;
    }
    if (n == 0) return 0;  // open peace (no last move to mirror yet)
    const int base = (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;
    if (n >= 6) {
      int lo = n - 6;
      int opp_war_rounds = 0, my_war = 0;
      for (int i = lo; i < n; ++i) {
        if (o1[i] == 1 || o2[i] == 1) ++opp_war_rounds;
        my_war += my[i];
      }
      if (opp_war_rounds >= 4 && my_war >= 4) {
        phase_ = 2;
        rem_ = 2 + draw_jitter();  // 1..3
        return 0;
      }
    }
    return base;
  }
};

// 6. GenZD3P: opens peace. p(peace) = 0.9/0.5/0.1 for an opponent last-round
//    war-count of 0/1/2.
class GenZD3P : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;
    const int c = (o1[n - 1] == 1) + (o2[n - 1] == 1);
    const double p_peace = (c == 0) ? 0.9 : (c == 1) ? 0.5 : 0.1;
    return (zoo_draw_uniform() < p_peace) ? 0 : 1;
  }
};

// 7/8. KLenient2 / KLenient3: war iff any opp has >=T wars in the last W
//    rounds (T,W = 2,3 for 2 and 3,4 for 3); a sub-threshold war activity
//    (1 <= opp wars < T) triggers with p 0.8 (forgive w.p. 0.2). After a
//    trigger: war 2 rounds, then amnesty of 2 +/- 1 rounds (jitter via rng).
class KLenient : public AgentBase {
  int thr_, win_;
  int phase_ = 0;  // 0 = normal, 1 = punish, 2 = amnesty
  int rem_ = 0;
 public:
  KLenient(int t, int w) : thr_(t), win_(w) {}

  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (phase_ == 1) {
      if (rem_ > 0) {
        --rem_;
        return 1;
      }
      phase_ = 2;
      rem_ = 2 + draw_jitter();  // 1..3
      return 0;
    }
    if (phase_ == 2) {
      --rem_;
      if (rem_ <= 0) phase_ = 0;
      return 0;
    }
    // normal
    if (n >= 1) {
      const int w1 = war_count_last(o1, win_);
      const int w2 = war_count_last(o2, win_);
      const int m = (w1 > w2) ? w1 : w2;
      bool trigger = false;
      if (m >= thr_)
        trigger = true;
      else if (m >= 1) {
        if (zoo_draw_uniform() >= 0.2) trigger = true;  // not forgiven
      }
      if (trigger) {
        phase_ = 1;
        rem_ = 1;  // this round's war + one more
        return 1;
      }
    }
    return 0;
  }
};

// 9. EndgameGTFT: GTFT-style one-shot forgiveness (isolated opp war ->
//    forgiven peace; repeated -> war) + deterministic endgame war ramp:
//    for round index t>=95, war w.p. min(1,(t-94)/12); t>=107 always war.
//    Final action = GTFT decision OR ramp draw.
class EndgameGTFT : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    int a = 0;
    if (n >= 1) {
      bool any = false, all_isolated = true;
      if (o1[n - 1] == 1) {
        any = true;
        if (n >= 2 && o1[n - 2] == 1) all_isolated = false;
      }
      if (o2[n - 1] == 1) {
        any = true;
        if (n >= 2 && o2[n - 2] == 1) all_isolated = false;
      }
      if (any && !all_isolated) a = 1;  // repeated -> war
      // isolated -> one-shot forgiveness -> stay peace
    }
    if (n >= 95) {
      double p = (static_cast<double>(n - 94) / 12.0);
      if (p > 1.0) p = 1.0;
      if (zoo_draw_uniform() < p) a = 1;
    }
    return a;
  }
};

// 10. HybridA: probe W at rd 5; burst W rds 11..13 (rate 0.6); then (n>=14):
//     silent non-retaliators (no opp war in rds 0..13) -> commit war;
//     probe-retaliators (any opp war in rds 0..13) -> TFT_any. Before the
//     probe (n<5) and between probe and burst (6..10) -> peace.
class HybridA : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n < 5) return 0;
    if (n == 5) return 1;  // probe
    if (n >= 11 && n <= 13)
      return (zoo_draw_uniform() < 0.6) ? 1 : 0;  // burst
    if (n >= 14) {
      const int pre = war_in_range(o1, 0, 14) + war_in_range(o2, 0, 14);
      if (pre == 0) return 1;  // silent -> commit war
      return (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;  // retaliators -> TFT
    }
    return 0;  // 6..10: silent gap -> peace
  }
};

// 11. HybridB: single probe W at rd 8; after that (n>=9): non-retaliators
//     (no opp war in rds 0..8) -> farming at rate 0.50; retaliators ->
//     TFT_any. Before the probe (n<8) -> peace.
class HybridB : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n < 8) return 0;
    if (n == 8) return 1;  // probe
    const int pre = war_in_range(o1, 0, 9) + war_in_range(o2, 0, 9);
    if (pre == 0) return (zoo_draw_uniform() < 0.50) ? 1 : 0;  // farming
    return (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;  // TFT_any
  }
};

// 12. FreqLearner: per-opponent lifetime war-rate, updated incrementally
//     (O(1) per round in normal order). War iff the average of the two
//     rates > 0.45, else peace.
class FreqLearner : public AgentBase {
  int processed_ = 0;
  int r1_ = 0, w1_ = 0, r2_ = 0, w2_ = 0;
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    for (int i = processed_; i < n; ++i) {
      ++r1_;
      w1_ += o1[i];
      ++r2_;
      w2_ += o2[i];
      ++processed_;
    }
    const double rate1 = (r1_ > 0) ? w1_ / static_cast<double>(r1_) : 0.0;
    const double rate2 = (r2_ > 0) ? w2_ / static_cast<double>(r2_) : 0.0;
    const double avg = (rate1 + rate2) / 2.0;
    return (avg > 0.45) ? 1 : 0;
  }
};

// 13. MarkovLearner: incremental transition counts P(next opp-war-count |
//     last opp-war-count) over the per-round opponent war-count {0,1,2}.
//     War iff the predicted next opponent war-count >= 1; fallback peace
//     until >= 8 observations.
class MarkovLearner : public AgentBase {
  int processed_ = 0;
  int last_ = 0;
  bool have_last_ = false;
  int trans_[3][3] = {};
  int total_obs_ = 0;
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    for (int i = processed_; i < n; ++i) {
      const int c = (o1[i] == 1) + (o2[i] == 1);
      if (have_last_) {
        ++trans_[last_][c];
        ++total_obs_;
      }
      last_ = c;
      have_last_ = true;
      ++processed_;
    }
    if (total_obs_ < 8) return 0;  // fallback peace
    const int row = trans_[last_][0] + trans_[last_][1] + trans_[last_][2];
    if (row == 0) return 0;
    const double pred =
        static_cast<double>(trans_[last_][1]) / row +
        2.0 * static_cast<double>(trans_[last_][2]) / row;
    return (pred >= 1.0) ? 1 : 0;
  }
};

// 14. CycleFitter: for each opponent, detect an exact period-2 or period-3
//     over its last 8 moves. Predicted-peace phase -> I war; predicted-war
//     phase -> I peace; non-periodic -> mirror (TFT_any). Emit war if either
//     opponent warrants it (OR).
class CycleFitter : public AgentBase {
 public:
  // Returns the predicted next move (0/1) if opp's last 8 moves are exactly
  // period-2 or period-3, else -1 (non-periodic).
  static int predict(const std::vector<int>& o, int n) {
    if (n < 8) return -1;
    int m[8];
    for (int i = 0; i < 8; ++i) m[i] = o[n - 8 + i];
    for (int p : {2, 3}) {
      bool ok = true;
      for (int i = 0; i < 8; ++i)
        if (i + p < 8 && m[i] != m[i + p]) {
          ok = false;
          break;
        }
      if (ok) return m[8 - p];  // next value = m[8-p] under period p
    }
    return -1;
  }

  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;
    const int p1 = predict(o1, n);
    const int p2 = predict(o2, n);
    const bool w1 = (p1 < 0) ? (o1[n - 1] == 1) : (p1 == 0);
    const bool w2 = (p2 < 0) ? (o2[n - 1] == 1) : (p2 == 0);
    return (w1 || w2) ? 1 : 0;
  }
};

// 15. AntiExploit: never first to war. Any opponent war -> war until BOTH
//     opponents give 3 consecutive all-peace rounds -> reset to peace. Never
//     initiates exploitation.
class AntiExploit : public AgentBase {
  bool engaged_ = false;
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (engaged_) {
      if (n >= 3 && war_count_last(o1, 3) == 0 && war_count_last(o2, 3) == 0)
        engaged_ = false;  // both calm for 3 consecutive rds -> reset
      return engaged_ ? 1 : 0;
    }
    if (n >= 1 && (o1[n - 1] == 1 || o2[n - 1] == 1)) {
      engaged_ = true;  // react to an opponent war
      return 1;
    }
    return 0;  // never initiate
  }
};

// 16. T4Tsmart: war iff BOTH opponents warred last round; opens peace. After
//     a round where I retaliated (warred), flip that retaliation to peace with
//     p 0.05 (forgiveness).
class T4Tsmart : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;
    int a = (o1[n - 1] == 1 && o2[n - 1] == 1) ? 1 : 0;
    if (a == 1 && my[n - 1] == 1 && zoo_draw_uniform() < 0.05)
      a = 0;  // 5% forgiveness flip after own retaliation
    return a;
  }
};

// =============================================================================
// Factories — one per registered name (name == the literal spec token).
// =============================================================================

AgentFactory make_agent_Prober_DDCC() {
  return [] { return std::make_unique<ProberDDCC>(); };
}
AgentFactory make_agent_SaintHunter() {
  return [] { return std::make_unique<SaintHunter>(); };
}
AgentFactory make_agent_Gradual3P() {
  return [] { return std::make_unique<Gradual3P>(); };
}
AgentFactory make_agent_CTFT3P() {
  return [] { return std::make_unique<CTFT3P>(); };
}
AgentFactory make_agent_Omega3P() {
  return [] { return std::make_unique<Omega3P>(); };
}
AgentFactory make_agent_GenZD3P() {
  return [] { return std::make_unique<GenZD3P>(); };
}
AgentFactory make_agent_KLenient2() {
  return [] { return std::make_unique<KLenient>(2, 3); };
}
AgentFactory make_agent_KLenient3() {
  return [] { return std::make_unique<KLenient>(3, 4); };
}
AgentFactory make_agent_EndgameGTFT() {
  return [] { return std::make_unique<EndgameGTFT>(); };
}
AgentFactory make_agent_HybridA() {
  return [] { return std::make_unique<HybridA>(); };
}
AgentFactory make_agent_HybridB() {
  return [] { return std::make_unique<HybridB>(); };
}
AgentFactory make_agent_FreqLearner() {
  return [] { return std::make_unique<FreqLearner>(); };
}
AgentFactory make_agent_MarkovLearner() {
  return [] { return std::make_unique<MarkovLearner>(); };
}
AgentFactory make_agent_CycleFitter() {
  return [] { return std::make_unique<CycleFitter>(); };
}
AgentFactory make_agent_AntiExploit() {
  return [] { return std::make_unique<AntiExploit>(); };
}
AgentFactory make_agent_T4Tsmart() {
  return [] { return std::make_unique<T4Tsmart>(); };
}

// Registration (names are the literal spec tokens).
REGISTER_AGENT(Prober_DDCC);
REGISTER_AGENT(SaintHunter);
REGISTER_AGENT(Gradual3P);
REGISTER_AGENT(CTFT3P);
REGISTER_AGENT(Omega3P);
REGISTER_AGENT(GenZD3P);
REGISTER_AGENT(KLenient2);
REGISTER_AGENT(KLenient3);
REGISTER_AGENT(EndgameGTFT);
REGISTER_AGENT(HybridA);
REGISTER_AGENT(HybridB);
REGISTER_AGENT(FreqLearner);
REGISTER_AGENT(MarkovLearner);
REGISTER_AGENT(CycleFitter);
REGISTER_AGENT(AntiExploit);
REGISTER_AGENT(T4Tsmart);

}  // namespace mas_sim
