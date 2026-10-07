// =============================================================================
// sim/candidates/candidates_f.cpp — slice C2a: candidate F "CandF" —
// endgame-ramp retaliator.
//
// Single concern: this one class, done precisely (+ its registration).
//
// Design: the backbone is the zoo CTFT3P rule-set (sim/zoo/zoo_t2.cpp),
// mirrored exactly as a stateless helper (R1) — the zoo is NOT modified.
// F adds exactly two concerns on top:
//   * R2 — the both-opponents override: if BOTH opponents warred last
//     round, war this round, unconditionally, BEFORE any forgiveness or
//     contrition;
//   * R3 — the endgame ramp: active only from n >= 90 (the horizon is
//     uniform on [90, 110]; the agent never sees H), in the parameterized
//     mode 0 (off) / 1 (threshold) / 2 (q-hazard).
//
// Precedence (evaluated top-down EVERY round; first match decides):
//   R0  n == 0 -> peace (open peace).
//   R2  OVERRIDE: o1[n-1] == 1 && o2[n-1] == 1 -> war (unconditional;
//       fires before R1's contrite/forgiveness path and before R3 — it
//       guarantees a war response in a mutual-war round even when R1
//       alone would give a contrite peace, and it takes the round away
//       from the ramp draw so a q-hazard miss can never skip a
//       both-opp-war response).
//   R3  ENDGAME RAMP (n >= 90 only; the agent never sees H, the window
//       starts at the minimum horizon):
//        mode 1 (threshold): war iff n >= ramp_T;
//        mode 2 (q-hazard):  war w.p. min(1, ramp_c / (111 - n)) — one
//              fast_rng() uniform draw per ramp round (the 111-n term is
//              the rounds remaining INCLUDING this round under H =
//              90 + round(20*u) in [90, 110]);
//        mode 0 (off): the rule never fires.
//       Ramp-wars are COMMITTED: R3 sits before R1, so once the ramp is
//       in play the CTFT3P contrite override (my last round war + both
//       opps peace -> peace) can never fire inside the window — there is
//       no contrition inside the ramp window, exactly as specified.
//   R1  BACKBONE (the CTFT3P mirror; the default fall-through):
//        base = (o1[n-1] == 1 || o2[n-1] == 1) ? 1 : 0;   // TFT_any
//        if (my[n-1] == 1 && o1[n-1] == 0 && o2[n-1] == 0)
//          return 0;                                      // contrite
//        return base;
//
// R2/R3 vs R1 precedence — flagged reading (dispatch collision): the
// dispatch names R2 as "the override ... unconditional, before
// forgiveness/contrition" while also numbering R2 after R1 and asking
// for a scripted "both-peace n >= T -> War" (test 3), which a R1-first
// ordering cannot satisfy (R1's contrite would fire on the would-be
// override round and R1's mirror-war would preempt the ramp on any
// prior war round). The operative reading is therefore the fixed
// per-round order R0 -> R2 -> R3 -> R1. Since R2 and R3 can only return
// WAR (they never return peace), this ordering can only REPLACE a
// R1-peace decision with a war; it can never replace a R1-war. Against a
// pair that never produces a both-opp-war round, R2 never fires, and
// the mode-0/1 ramp fires only on rounds where R1 would have peaced
// (a contrite round or a both-peace round), so CandF (mode 0) never
// loses to the same-seed CTFT3P backbone (the dispatch tests (1)/(2):
// "the override must not lose").
//
// Conventions (sim/README.md §C1a/C1b + zoo/zoo_common.hpp):
//   * n == number of completed rounds; "rd k" is the 0-based index k.
//   * RNG: draws ONLY from the engine thread-local fast_rng(); the
//     q-hazard p is always in (0, 1] for the legal n range, so the
//     mode-2 ramp round always consumes exactly one uniform() draw (the
//     0/1 short-circuit without a draw never triggers here).
//   * Stateless per call (like CTFT3P): the engine constructs a fresh
//     agent per match, so nothing else to keep.
//   * Registered via REGISTER_AGENT(CandF) and the five named variants.
// =============================================================================

#include "candidates_f.hpp"

#include "../zoo/zoo_common.hpp"  // fast_rng() (RNG contract)

namespace mas_sim {

namespace {

// R1: the CTFT3P rule-set from sim/zoo/zoo_t2.cpp, mirrored exactly
// (stateless; O(1)):
//     if (n == 0) return 0;
//     const int base = (o1[n-1] == 1 || o2[n-1] == 1) ? 1 : 0;
//     if (my[n-1] == 1 && o1[n-1] == 0 && o2[n-1] == 0) return 0;  // contrite
//     return base;
int ctft3p_base(int n, const std::vector<int>& my,
                const std::vector<int>& o1, const std::vector<int>& o2) {
  const int base = (o1[n - 1] == 1 || o2[n - 1] == 1) ? 1 : 0;
  if (my[n - 1] == 1 && o1[n - 1] == 0 && o2[n - 1] == 0) return 0;  // contrite
  return base;
}

}  // namespace

// =============================================================================
// Candidate F — CandF (endgame-ramp retaliator)
// =============================================================================
class CandF : public AgentBase {
  CandFParams p_;

 public:
  explicit CandF(const CandFParams& p) : p_(p) {}

  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    // R0: open peace.
    if (n == 0) return 0;

    // R2: the unconditional override — both opponents warred last round.
    if (o1[n - 1] != 0 && o2[n - 1] != 0) return 1;

    // R3: the endgame ramp (window n >= 90; the agent never sees H).
    if (n >= 90) {
      if (p_.ramp_mode == 1) {
        if (n >= p_.ramp_T) return 1;  // threshold: committed war
      } else if (p_.ramp_mode == 2) {
        // q-hazard: p = min(1, ramp_c / (111 - n)). For n >= 90 the
        // denominator is positive (<= 21), so p is in (0, 1]: exactly
        // one uniform() draw per ramp round.
        double p = p_.ramp_c / static_cast<double>(111 - n);
        if (p > 1.0) p = 1.0;
        if (fast_rng().uniform() < p) return 1;
      }
      // mode 0 (off): never fires.
    }

    // R1: the CTFT3P backbone (default; R0 already excluded n == 0).
    return ctft3p_base(n, my, o1, o2);
  }
};

// =============================================================================
// Factories
// =============================================================================

AgentFactory make_CandF(const CandFParams& p) {
  return [p] { return std::make_unique<CandF>(p); };
}

// Named variants (registered; names are the literal dispatch tokens).
AgentFactory make_CandF_T104() {
  CandFParams p;
  p.ramp_mode = 1;
  p.ramp_T = 104;
  return make_CandF(p);
}

AgentFactory make_CandF_T106() {
  CandFParams p;
  p.ramp_T = 106;  // default mode 1
  return make_CandF(p);
}

AgentFactory make_CandF_T108() {
  CandFParams p;
  p.ramp_mode = 1;
  p.ramp_T = 108;
  return make_CandF(p);
}

AgentFactory make_CandF_mode2() {
  CandFParams p;
  p.ramp_mode = 2;
  return make_CandF(p);
}

AgentFactory make_CandF_mode0() {
  CandFParams p;
  p.ramp_mode = 0;
  return make_CandF(p);
}

// Registry-facing factories.
AgentFactory make_agent_CandF() { return make_CandF(); }
AgentFactory make_agent_CandF_T104() { return make_CandF_T104(); }
AgentFactory make_agent_CandF_T106() { return make_CandF_T106(); }
AgentFactory make_agent_CandF_T108() { return make_CandF_T108(); }
AgentFactory make_agent_CandF_mode2() { return make_CandF_mode2(); }
AgentFactory make_agent_CandF_mode0() { return make_CandF_mode0(); }

// Link-time registration (names are the literal dispatch tokens).
REGISTER_AGENT(CandF);
REGISTER_AGENT(CandF_T104);
REGISTER_AGENT(CandF_T106);
REGISTER_AGENT(CandF_T108);
REGISTER_AGENT(CandF_mode2);
REGISTER_AGENT(CandF_mode0);

}  // namespace mas_sim
