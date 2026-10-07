// =============================================================================
// sim/candidates/candidates_f.hpp — slice C2a: parameterized API for the
// candidate F arm (CandF — endgame-ramp retaliator).
//
// F = the zoo CTFT3P backbone (sim/zoo/zoo_t2.cpp, mirrored exactly as a
// stateless helper) + an unconditional both-opponents-warred override (R2)
// + a committed endgame war ramp (R3) that activates at n >= 90 (the
// horizon is uniform on [90, 110]; the agent never sees H).
//
// Added beyond a single .cpp on purpose: "params exposed in struct +
// factory" requires a cross-TU declaration so the test TU can build the
// named variants (T98/T100/T104/T106/T108/mode2/mode0). Implementation +
// registration live in candidates_f.cpp.
// =============================================================================

#ifndef MAS_SIM_CANDIDATES_F_HPP
#define MAS_SIM_CANDIDATES_F_HPP

#include "../engine.hpp"

namespace mas_sim {

// Candidate F "CandF" — endgame-ramp retaliator.
//
// Parameter meanings:
//   * ramp_mode: 0 = OFF (pure R0+R1+R2, the deterministic CTFT3P-mirror
//     arm — no endgame ramp at all);
//                1 = THRESHOLD — war from n >= ramp_T (deterministic);
//                2 = Q-HAZARD — war w.p. min(1, ramp_c / (111 - n)) each
//     round, one fast_rng draw (the q-hazard: 111-n is the number of
//     rounds remaining INCLUDING the current one under the horizon model
//     H = 90 + round(20*u) in [90, 110]).
//   * ramp_T: the threshold round for mode 1 (the ramp war round).
//   * ramp_c: the hazard constant for mode 2 (expected remaining wars in
//     the endgame window).
//
// Ramp-wars are COMMITTED: inside the window (n >= 90) the ramp (R3) is
// evaluated before the backbone (R1), so the CTFT3P contrite override
// (warred last round while both opps peaced -> peace) can never suppress
// a ramp war. The agent never sees H; the window is simply "n >= 90".
struct CandFParams {
  int ramp_mode = 1;   // 0 off / 1 threshold / 2 q-hazard
  int ramp_T = 106;    // mode 1: war iff n >= ramp_T
  double ramp_c = 3.0;  // mode 2: p = min(1, ramp_c / (111 - n))
};

// Parameterized factory (the public API for campaign-level F use).
AgentFactory make_CandF(const CandFParams& p = CandFParams());

// Named variants (registered alongside each other):
//   CandF_T98  = mode 1, ramp_T = 98;
//   CandF_T100 = mode 1, ramp_T = 100;
//   CandF_T104 = mode 1, ramp_T = 104;
//   CandF_T106 = mode 1, ramp_T = 106 (the default arm);
//   CandF_T108 = mode 1, ramp_T = 108;
//   CandF_mode2 = mode 2 (q-hazard, default ramp_c);
//   CandF_mode0 = mode 0 (ramp off; the pure deterministic mirror).
AgentFactory make_CandF_T98();
AgentFactory make_CandF_T100();
AgentFactory make_CandF_T102();
AgentFactory make_CandF_T104();
AgentFactory make_CandF_T106();
AgentFactory make_CandF_T108();
AgentFactory make_CandF_mode2();
AgentFactory make_CandF_mode0();

}  // namespace mas_sim

#endif  // MAS_SIM_CANDIDATES_F_HPP
