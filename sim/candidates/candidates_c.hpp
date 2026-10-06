// =============================================================================
// sim/candidates/candidates_c.hpp — slice C1b: parameterized API for the
// candidate C arm (CandC_Classifier, pure two-test classifier + exploiter).
//
// Added beyond a single .cpp on purpose: "params exposed in struct +
// factory" requires a cross-TU declaration so the test TU can build the
// deterministic (rate/r=0/1) variants. Implementation + registration live
// in candidates_c.cpp.
// =============================================================================

#ifndef MAS_SIM_CANDIDATES_C_HPP
#define MAS_SIM_CANDIDATES_C_HPP

#include "../engine.hpp"

namespace mas_sim {

// Candidate C "CandC_Classifier" — pure exploitation arm (no k-lenient
// backbone, no anti-lock, no dithering beyond probe stochasticity).
struct CandCParams {
  int probe_rd = 4;       // the single guaranteed probe-war round
  int burst_start = 11;   // first of `burst_len` burst-war rounds
  int burst_len = 3;      // burst rounds: [burst_start, burst_start+burst_len)
  double burst_rate = 0.6;   // war w.p. burst_rate on each burst round (drawn)
  double trickle_rate = 0.1; // war w.p. trickle_rate on the other probe-window
                             // rounds [probe_rd+1, burst_start+burst_len)
  int classify_end = 14;  // last probe-script round; joint classification
                          // happens on the first call with n >= classify_end+1
  double r_star = 0.40;   // exploit-phase farm war probability per round (iid
                          // fast_rng draw; 0/1 short-circuit WITHOUT a draw)
  int nasty_win = 8;      // window (in rounds) for the hopeless war-rate test
  double nasty_thresh = 0.7;  // hopeless if that window's war-rate >= thresh
};

// Parameterized factory (the public API for campaign-level use).
AgentFactory make_CandC_Classifier(const CandCParams& p = CandCParams());

}  // namespace mas_sim

#endif  // MAS_SIM_CANDIDATES_C_HPP
