// =============================================================================
// sim/candidates/candidates_e.hpp — slice C1c: parameterized API for the
// candidate E arm (CandE_Hybrid — the ship candidate).
//
// E = CandB-style k-lenient backbone + CandC-style two-test classifier +
// anti-lock "squeeze-commit" + class-conditional endgame (GAME-ANALYSIS §5).
//
// Added beyond a single .cpp on purpose: "params exposed in struct +
// factory" requires a cross-TU declaration so the test TU can build the
// deterministic (rate/eps/g/r* = 0/1) variants. Implementation +
// registration live in candidates_e.cpp.
// =============================================================================

#ifndef MAS_SIM_CANDIDATES_E_HPP
#define MAS_SIM_CANDIDATES_E_HPP

#include "../engine.hpp"

namespace mas_sim {

// Candidate E "CandE_Hybrid" — the hybrid ship candidate.
//
// Parameter meanings (defaults are the design-synthesis values from
// POOLS.md §4):
//   * probe/burst/trickle/classify: the R4 conditional probe script
//     (identical geometry to CandC; see the .cpp for the exact windows).
//   * r_star: base farm war-rate for the exploit phase (drawn each
//     post-classify round; 0/1 short-circuit WITHOUT a draw).
//   * partner_adj: the silent-farming partner adjustment. The effective
//     farming rate is r = r_star + partner_adj*(partner_warrate - 0.3),
//     clamped to [0.2, 0.55]; partner_warrate = the paired opp's all-history
//     war-rate (the other stream, so a silent opp's own history stays clean
//     for the flip detector).
//   * leniency_eps: an isolated single-opp war is forgiven w.p.
//     leniency_eps, drawn BEFORE counting (CandB convention; 0/1
//     short-circuit without a draw).
//   * punish_len / amnesty_len / jitter: the CandB backbone phases (the
//     backbone is active only during the classification window, i.e.
//     pre-commit; the punish includes its trigger round; the amnesty is
//     jittered +/-jitter, clamped >=1).
//   * endgame_responsive / endgame_exploit: the class-conditional endgame
//     switch round (responsive/unclean >= responsive -> war; exploit >=
//     exploit -> war).
//   * nasty_thresh / nasty_win: hopeless if a stream's last nasty_win-rd
//     war-rate >= nasty_thresh.
//   * flip_win: the post-classify window — >=2 opp wars within any
//     flip_win-rd window flips the pair to responsive handling.
//   * feud_break_len / feud_min_streak: the R1.5 contrite feud-break
//     (Wu & Axelrod CTFT / Omega-style de-escalation) — after a clean
//     pre-probe phase, >=feud_min_streak consecutive both-opp war rounds
//     that echo my own recent wars trigger `feud_break_len` unilateral
//     peace rounds (suppressing R1/R2/R2-amnesty).
struct CandEParams {
  int probe_rd = 4;            // the single conditional probe war round
  int burst_start = 11;        // first of `burst_len` burst rounds
  int burst_len = 3;           // burst rounds [burst_start, burst_start+burst_len)
  double burst_rate = 0.6;     // each in-window burst round wars w.p. burst_rate
  double trickle_rate = 0.1;   // each in-window non-burst round wars w.p. trickle
  int classify_end = 14;       // last probe-script round; classify at n>=classify_end+1
  double r_star = 0.40;        // base exploit-farm war-rate (before partner adj)
  double partner_adj = 0.05;   // partner-adjusted farming-rate slope
  double leniency_eps = 0.2;   // isolated single-war forgiveness probability
  int punish_len = 2;          // punish phase length (war rds, incl. trigger)
  int amnesty_len = 3;         // amnesty base length (peace rds)
  int jitter = 1;              // amnesty jittered +/-jitter (one fast_rng draw)
  int endgame_responsive = 105; // endgame war round for responsive/unclean pairs
  int endgame_exploit = 90;     // endgame war round for exploitable-silent pairs
  double nasty_thresh = 0.7;    // hopeless if window war-rate >= nasty_thresh
  int nasty_win = 8;            // hopeless war-rate window (rounds)
  int flip_win = 5;             // flip detector: 2 wars within any flip_win-rd window
  int feud_break_len = 2;      // R1.5: unilateral peace rounds of the feud-break
  int feud_min_streak = 3;     // R1.5: min consecutive both-opp war-streak to fire
};

// Parameterized factory (the public API for campaign-level E use).
AgentFactory make_CandE_Hybrid(const CandEParams& p = CandEParams());

}  // namespace mas_sim

#endif  // MAS_SIM_CANDIDATES_E_HPP
