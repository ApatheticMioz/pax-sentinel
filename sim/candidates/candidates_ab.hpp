// =============================================================================
// sim/candidates/candidates_ab.hpp — slice C1a: parameterized API for the two
// candidate control/baseline bots (CandA_GTFT, CandB_KLenient).
//
// Added beyond the dispatch's two-file list on purpose: "params exposed in
// struct + factory" requires a cross-TU declaration so the test TU can build
// the deterministic g/eps variants. Implementation + registration live in
// candidates_ab.cpp.
// =============================================================================

#ifndef MAS_SIM_CANDIDATES_AB_HPP
#define MAS_SIM_CANDIDATES_AB_HPP

#include "../engine.hpp"

namespace mas_sim {

// Candidate A "CandA_GTFT" (GTFT-3P baseline) parameters.
struct CandAParams {
  double g = 0.5;  // forgive (play peace) an isolated single-opp offense
                   // with probability g (fast_rng draw; 0/1 short-circuits
                   // without a draw, making those variants deterministic).
};

// Candidate B "CandB_KLenient" (k-lenient dithered retaliator) parameters.
struct CandBParams {
  int window = 3;      // rolling window of opponent rounds inspected
  int thresh = 2;      // trigger: any opp with >= thresh wars in the window
  int punish = 2;      // punish phase length in war rounds (incl. trigger rd)
  int amnesty = 3;     // amnesty base length in peace rounds
  double eps = 0.2;    // sub-threshold ("isolated") war forgiven w.p. eps
                       // (drawn BEFORE counting; 0/1 short-circuits without a
                       // draw, making those variants deterministic)
  int jitter = 1;      // amnesty length jittered by a uniform draw to
                       // {amnesty-jitter, ..., amnesty+jitter}
};

// Parameterized factories (the public API for campaign-level A/B use).
AgentFactory make_CandA_GTFT(const CandAParams& p = CandAParams());
AgentFactory make_CandB_KLenient(const CandBParams& p = CandBParams());

}  // namespace mas_sim

#endif  // MAS_SIM_CANDIDATES_AB_HPP
