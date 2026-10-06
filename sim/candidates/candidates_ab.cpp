// =============================================================================
// sim/candidates/candidates_ab.cpp — slice C1a: candidate bots A and B
//
// Single concern: the two candidate control/baseline bots + their
// registration. NO candidate C/E, NO campaign runs (later slices).
//
//   * CandA_GTFT  — GTFT-3P baseline: opens peace; war iff >=1 opp warred
//     last rd, EXCEPT forgive (peace) when exactly one opp warred last rd
//     AND that opp had zero wars in rds n-4..n-2 (isolated offense) —
//     forgive w.p. g (default 0.5); if BOTH warred last rd -> always war.
//   * CandB_KLenient — k-lenient dithered retaliator: opens peace; trigger
//     = any opp with >= thresh wars in the last `window` rounds; a trigger
//     starts a punish phase (punish war rounds, incl. the trigger round)
//     followed by an amnesty of `amnesty` +/- `jitter` peace rounds
//     (jitter drawn from fast_rng). Isolated (sub-threshold) single wars
//     are forgiven w.p. eps, with the draw made BEFORE counting (spec).
//
// Distinct from the zoo: zoo_t2.cpp's KLenient2/KLenient3 are fixed-
// parameter zoo bots; these candidates are SEPARATE parameterized
// implementations (own classes, no shared code) per the dispatch.
//
// Conventions (sim/README.md §B2/B3 + zoo_t2.cpp):
//   * n == number of completed rounds (0-based round-index convention);
//     every bot has an n==0 guard and opens peace.
//   * RNG: draws ONLY from the engine thread-local fast_rng() (never
//     ::rand(), never time-seeded); the engine reseeds it from the run's
//     seed, so a run stays a pure function of (pool, seed, mode, kind).
//     Degenerate probabilities (0/1) short-circuit WITHOUT a draw so the
//     g/eps=0/1 variants are fully deterministic (flagged reading; same
//     practice as the zoo "deterministic variant" tests).
//   * O(window) per round; match-local member state; the engine constructs
//     a fresh agent per match, so state resets naturally per match.
//   * Registered via REGISTER_AGENT(CandA_GTFT) / (CandB_KLenient) with
//     default params; the parameterized factories above are the API.
// =============================================================================

#include "candidates_ab.hpp"

#include "../zoo/zoo_common.hpp"  // fast_rng(), zoo conventions (RNG contract)

namespace mas_sim {

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

// Map one uniform draw to a jitter in {-1, 0, +1} (three equal thirds).
inline int draw_jitter() {
  const double u = fast_rng().uniform();
  return (u < 1.0 / 3.0) ? -1 : (u < 2.0 / 3.0) ? 0 : 1;
}

}  // namespace

// =============================================================================
// Candidate A — CandA_GTFT (GTFT-3P baseline)
// =============================================================================
class CandAGTFT : public AgentBase {
  CandAParams p_;

 public:
  explicit CandAGTFT(const CandAParams& p) : p_(p) {}

  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;  // open peace
    const int w1 = o1[n - 1];
    const int w2 = o2[n - 1];
    if (w1 && w2) return 1;  // both warred last rd -> always war
    if (!w1 && !w2) return 0;
    // Exactly one opponent warred last round.
    const std::vector<int>& o = w1 ? o1 : o2;
    // Isolated offense: zero wars by that opp in rds n-4..n-2 (the three
    // rounds before the last), clamped to history start.
    int lo = n - 4;
    if (lo < 0) lo = 0;
    int prev_wars = 0;
    for (int i = lo; i < n - 1; ++i) prev_wars += o[i];
    if (prev_wars != 0) return 1;  // non-isolated -> war
    // Isolated -> forgive w.p. g.
    if (p_.g >= 1.0) return 0;
    if (p_.g <= 0.0) return 1;
    return (fast_rng().uniform() < p_.g) ? 0 : 1;
  }
};

// =============================================================================
// Candidate B — CandB_KLenient (k-lenient dithered retaliator)
// =============================================================================
class CandBKLenient : public AgentBase {
  CandBParams p_;
  int phase_ = 0;  // 0 = normal, 1 = punishing, 2 = amnesty
  int rem_ = 0;    // remaining phase rounds AFTER the current call, per the
                   // zoo_t2 KLenient bookkeeping convention

 public:
  explicit CandBKLenient(const CandBParams& p) : p_(p) {}

  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (phase_ == 1) {  // punishing: `punish` war rounds total (incl. trigger)
      if (rem_ > 0) {
        --rem_;
        return 1;
      }
      // Punish done -> enter amnesty: total peace length =
      // amnesty +/- jitter (one draw when jitter > 0).
      int len = p_.amnesty + (p_.jitter > 0 ? draw_jitter() : 0);
      if (len < 1) len = 1;
      phase_ = 2;
      rem_ = len - 1;  // this round is the first peace round
      return 0;
    }
    if (phase_ == 2) {  // amnesty: peace, no draws
      --rem_;
      if (rem_ <= 0) phase_ = 0;
      return 0;
    }
    if (n == 0) return 0;  // open peace
    // Trigger evaluation: the forgiveness draw happens BEFORE counting
    // (spec). Degenerate eps (0/1) short-circuits without a draw.
    const bool strict = p_.eps > 0.0 && p_.eps < 1.0;
    double u = 0.0;
    if (strict) u = fast_rng().uniform();
    const int w1 = war_count_last(o1, p_.window);
    const int w2 = war_count_last(o2, p_.window);
    bool trigger = false;
    if (w1 >= p_.thresh || w2 >= p_.thresh) {
      trigger = true;  // full threshold: war, no forgiveness
    } else if (w1 >= 1 || w2 >= 1) {
      // Isolated (sub-threshold) war: forgiven w.p. eps.
      if (p_.eps <= 0.0)
        trigger = true;
      else if (p_.eps >= 1.0)
        trigger = false;
      else
        trigger = (u >= p_.eps);
    }
    if (trigger) {
      phase_ = 1;
      int p = p_.punish;
      if (p < 1) p = 1;
      rem_ = p - 1;  // this round is the first punish war
      return 1;
    }
    return 0;
  }
};

// =============================================================================
// Factories
// =============================================================================

AgentFactory make_CandA_GTFT(const CandAParams& p) {
  return [p] { return std::make_unique<CandAGTFT>(p); };
}

AgentFactory make_CandB_KLenient(const CandBParams& p) {
  return [p] { return std::make_unique<CandBKLenient>(p); };
}

// Registry-facing factories: default-parameter instances.
AgentFactory make_agent_CandA_GTFT() { return make_CandA_GTFT(); }
AgentFactory make_agent_CandB_KLenient() { return make_CandB_KLenient(); }

// Link-time registration (names are the literal dispatch tokens).
REGISTER_AGENT(CandA_GTFT);
REGISTER_AGENT(CandB_KLenient);

}  // namespace mas_sim
