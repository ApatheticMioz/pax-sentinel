// =============================================================================
// sim/candidates/candidates_g.cpp — slice C2d: candidate G "CandG_TolRamp" —
// the "coast + harvest" hybrid.
//
// Single concern: this one class, done precisely (+ its registration).
//
// Design: the backbone is the zoo TolerantW5 rule-set (sim/zoo/zoo_t01.cpp,
// the TolerantW class at w = 5), mirrored EXACTLY as a stateless-equivalent
// helper (R1) — the zoo is NOT modified. G adds exactly one concern on top:
//   * R2 — the committed endgame war ramp: war iff n >= ramp_T (default 104).
//
// Precedence (evaluated top-down EVERY round; first match decides):
//   R0  n == 0 -> peace (open peace — TolerantW5 opens peace too).
//   R2  ENDGAME RAMP (mode 1): war iff n >= ramp_T. Evaluated BEFORE R1, so
//       once the ramp is in play (n >= ramp_T) the TolerantW5 windowed
//       war>peace decision can never suppress a ramp war. TolerantW5 carries
//       no contrition, so there is nothing else to override; the "commit"
//       is purely the ramp's precedence over the backbone inside the window.
//       The agent never sees H; the window is "n >= ramp_T".
//   R1  BACKBONE (the TolerantW5 mirror; the default fall-through):
//        lo = max(0, n - 5);
//        count peace/war over both opponents' rounds [lo, n);
//        return war > peace ? 1 : 0;
//
// Backbone state handling — mirrored precisely (the "cumulative/window"
// state the dispatch refers to): TolerantW5 keeps NO member state. Each
// round it re-derives its decision by rescanning the LAST `w` = 5
// opponent rounds (clamped to the history when n < 5) over BOTH opponents
// and waring only if the total war count strictly exceeds the total peace
// count (a tie stays peace; `war > peace`, never `>=`). The per-round
// rescan (O(window)) IS the entire state handling of the zoo bot, and the
// mirror below reproduces it bit-for-bit — including the n==0 -> 0 short
// circuit and the two separate peace/war accumulators (one pass per
// opponent, exactly as written in zoo_t01.cpp).
//
// "No probes, own PRNG if any randomness needed": G is fully deterministic
// (the ramp is a pure threshold, the backbone is a pure count); it makes NO
// RNG draw at all, so there is no RNG to own. This keeps the R2-never-fires
// control (ramp_T = 111) and the backbone-equivalence gate exact by
// construction.
//
// Conventions (sim/README.md §C1a/C1b + zoo/zoo_common.hpp):
//   * n == number of completed rounds; "rd k" is the 0-based index k.
//   * Stateless per call (like the zoo TolerantW5): the engine constructs a
//     fresh agent per match, so nothing else to keep.
//   * Registered via REGISTER_AGENT(CandG_TolRamp).
// =============================================================================

#include "candidates_g.hpp"

#include "../zoo/zoo_common.hpp"  // shared contract header (fast_rng contract;
                                  // G makes no RNG draw — deterministic arm)

namespace mas_sim {

namespace {

// R1: the TolerantW5 rule-set from sim/zoo/zoo_t01.cpp (the TolerantW class
// at w = 5), mirrored EXACTLY. The zoo bot is stateless per call: each round
// it rescans the last `w` rounds over BOTH opponents and wars iff the window
// war count strictly exceeds the window peace count. `w` is pinned to 5 (the
// TolerantW5 registration); the n == 0 short-circuit lives in selectAction
// (R0) and is kept here too so this helper is safe at any n (the zoo's
// `if (n == 0) return 0;` is identical — R0 is the same condition).
//
// State handling replicated precisely: no member state; the decision is a
// pure function of (n, the last-5 slice of each opponent's history). The
// clamped window (lo = max(0, n - 5)), the two accumulators (one pass per
// opponent), and the strict `war > peace` comparison are all bit-identical
// to the zoo implementation.
int tolerantw5_base(int n, const std::vector<int>&, const std::vector<int>& o1,
                    const std::vector<int>& o2) {
  if (n == 0) return 0;
  const int w = 5;
  int lo = n - w;
  if (lo < 0) lo = 0;
  int peace = 0, war = 0;
  for (int i = lo; i < n; ++i) (o1[i] == 0) ? ++peace : ++war;
  for (int i = lo; i < n; ++i) (o2[i] == 0) ? ++peace : ++war;
  return war > peace ? 1 : 0;
}

}  // namespace

// =============================================================================
// Candidate G — CandG_TolRamp ("coast + harvest" hybrid)
// =============================================================================
class CandG : public AgentBase {
  CandGParams p_;

 public:
  explicit CandG(const CandGParams& p) : p_(p) {}

  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    // R0: open peace (n == 0).
    if (n == 0) return 0;

    // R2: the committed endgame war ramp (mode 1: war iff n >= ramp_T).
    // Evaluated BEFORE R1 so the TolerantW5 backbone can never suppress a
    // ramp war once inside the window (no contrition to override — TolerantW5
    // has none — the commit is purely this precedence). The agent never sees
    // H; the window is "n >= ramp_T".
    if (n >= p_.ramp_T) return 1;

    // R1: the TolerantW5 backbone (default fall-through; R0 already excluded
    // n == 0, but the helper is safe at any n regardless).
    return tolerantw5_base(n, my, o1, o2);
  }
};

// =============================================================================
// Factories
// =============================================================================

AgentFactory make_CandG(const CandGParams& p) {
  return [p] { return std::make_unique<CandG>(p); };
}

// Named variant (registered; the shipped arm at the default ramp_T = 104).
AgentFactory make_CandG_TolRamp() {
  CandGParams p;
  p.ramp_T = 104;  // default (CandGParams() default) — ramp war from n >= 104
  return make_CandG(p);
}

// Registry-facing factory.
AgentFactory make_agent_CandG_TolRamp() { return make_CandG_TolRamp(); }

// Link-time registration (name is the literal dispatch token).
REGISTER_AGENT(CandG_TolRamp);

}  // namespace mas_sim
