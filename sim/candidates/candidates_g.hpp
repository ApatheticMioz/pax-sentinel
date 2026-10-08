// =============================================================================
// sim/candidates/candidates_g.hpp — slice C2d: parameterized API for the
// candidate G arm (CandG_TolRamp — "coast + harvest" hybrid).
//
// G = the zoo TolerantW5 backbone (sim/zoo/zoo_t01.cpp) mirrored EXACTLY as a
// stateless-equivalent helper (R1) + a committed endgame war ramp (R2) that
// activates at n >= ramp_T (default 104; the horizon is uniform on [90, 110]
// and the agent never sees H). The zoo is NOT modified by this slice.
//
// Added beyond a single .cpp on purpose: "params exposed in struct +
// factory" requires a cross-TU declaration so the test TU can build the
// never-fires variant (ramp_T = 111 > max horizon 110). Implementation +
// registration live in candidates_g.cpp.
// =============================================================================

#ifndef MAS_SIM_CANDIDATES_G_HPP
#define MAS_SIM_CANDIDATES_G_HPP

#include "../engine.hpp"

namespace mas_sim {

// Candidate G "CandG_TolRamp" — "coast + harvest" hybrid.
//
// Parameter meanings:
//   * ramp_T: the threshold round at which the committed endgame war ramp
//     fires (mode 1: war iff n >= ramp_T). Default 104. The agent never
//     sees the horizon H; the window is simply "n >= ramp_T". With the
//     shipped value (104) the ramp fires inside the legal horizon window
//     [90, 110]; with 111 it never fires (a never-fires control).
//
// The ramp war is COMMITTED: inside the window (n >= ramp_T) the ramp (R2)
// is evaluated before the backbone (R1), so the TolerantW5 windowed
// war>peace decision can never suppress a ramp war (TolerantW5 has no
// contrition; the "commit" is purely the ramp's precedence over R1).
//
// Registered name: CandG_TolRamp (default ramp_T = 104).
struct CandGParams {
  int ramp_T = 104;  // war iff n >= ramp_T (committed; default 104)
};

// Parameterized factory (the public API for campaign-level G use).
AgentFactory make_CandG(const CandGParams& p = CandGParams());

// Named variant (registered): the shipped arm at the default ramp_T = 104.
AgentFactory make_CandG_TolRamp();

}  // namespace mas_sim

#endif  // MAS_SIM_CANDIDATES_G_HPP
