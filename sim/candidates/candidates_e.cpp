// =============================================================================
// sim/candidates/candidates_e.cpp — slice C1c: candidate E "CandE_Hybrid", the
// HYBRID SHIP CANDIDATE.
//
// Single concern: this one class, done precisely (+ its registration). NO
// campaign runs (later slices). E is a NEW class, not a wrapper around
// CandB/CandC: it reuses their patterns (CandB's window-phase machine + the
// before-counting leniency draw; CandC's two-test classifier, the one-way
// flip test, and the TFT-any one-shot g=0.5 forgiveness) and fuses them with
// the two E-specific additions — the R1 anti-lock "squeeze-commit" and the
// R5 partner-adjusted farming + late-switch guard — into one precedence
// machine.
//
// Precedence (evaluated top-down EVERY round; the first match decides):
//   R0  n == 0 -> peace (open peace, all states).
//   R1  SQUEEZE-COMMIT (anti-lock): both opps warred on each of the last
//       >=2 consecutive rounds AND my war-streak >=3 -> commit to permanent
//       war for this match (0 > -1: never peace into a sustained squeeze).
//       Reset: both opps all-peace for 2 consecutive rounds lifts it.
//   R1.5 FEUD-BREAK (contrite de-escalation; Wu & Axelrod CTFT / Omega-style,
//       between R1 and R2): fires only when BOTH (a) NEITHER opponent ever
//       warred before my first war this match (preProbeClean) and (b) the
//       COPYING-ECHO pattern holds — the pair's both-opp war-streak is >=
//       feud_min_streak and each opponent's war in each of the last 2
//       rounds coincides with my war the round before (they copy ME, so
//       their wars are the echo of my probe, not independent hostility).
//       ACTION: `feud_break_len` rounds of unilateral peace, SUPPRESSING R1
//       squeeze-commit, the R2 backbone trigger, and R2-amnesty machinery
//       (R3 endgame still applies: a break round at n >= endgame_responsive
//       wars, aborting the break). Mid-break abort: an opponent wars in a
//       break round while my prior action was peace (genuine hostility, not
//       an echo) -> abort immediately; the machine resumes with a fresh R2
//       punish (its leniency draw is made as usual, so a lone war is
//       forgiven w.p. leniency_eps — the standard machine, nothing special).
//       After the break COMPLETES: phase_/rem_ (punish/amnesty) are cleared,
//       the machine stays kNormal, and the R6 peace default resumes;
//       preProbeClean remains true, so a recurring echo re-enters the break.
//       Hostility safety: vs a pair that warred before my first war
//       (e.g. the Nasty-pair from rd 0) preProbeClean is false and the
//       rule can never fire (it de-escalates only self-started feuds).
//   R2  BACKBONE (CandB-style; active ONLY during the classification
//       window, i.e. pre-commit, and when the pair is not classified
//       exploitable): any opp with >=2 wars in the last 3 rounds -> a
//       punish phase of punish_len war rounds (incl. the trigger round,
//       CandB bookkeeping) followed by an (amnesty_len +/- jitter) peace
//       phase; a single isolated (sub-threshold) war is forgiven w.p.
//       leniency_eps, with the draw made BEFORE counting (CandB convention;
//       0/1 short-circuit without a draw).
//   R3  ENDGAME: responsive/uncertain pair -> war once n >=
//       endgame_responsive; classified exploitable-silent pair -> war once
//       n >= endgame_exploit. In both regimes the war is checked BEFORE any
//       other post-classify action, so it supersedes a pending backbone
//       amnesty (R3 is top-priority in each post-classify branch).
//   R4  CLASSIFICATION WINDOW (deciding rounds 0..classify_end):
//       * conditional probe: war at probe_rd ONLY IF neither opp warred in
//         rds 0..probe_rd-1 (skip when already hostile — saves the C-arm's
//         demonstrated all-war lock);
//       * burst rounds [burst_start, burst_start+burst_len) war w.p.
//         burst_rate ONLY IF zero retaliation has been observed so far (any
//         opp war anywhere in the window aborts the burst immediately);
//       * trickle rounds (other in-window rounds) war w.p. trickle_rate;
//       * R2 supersedes R4 on conflict (a backbone decision overrides the
//         script for that round).
//   R5  POST-CLASSIFY joint class (the first call with n >= classify_end+1;
//       the same CandC two-test classifier per stream: hopeless / responsive
//       / silent), then a joint pair class:
//       (a)  either hopeless -> HOPeless: permanent war;
//       (b)  either responsive -> RESPONSIVE: TFT_any with one-shot
//           isolated-war forgiveness at g = 0.5 (CandA/CandC style) — this
//           is a clean memoryless mirror (no backbone machine), so the
//           machine's last window phase does not leak into the responsive
//           phase;
//       (c)  both silent -> EXPLOITABLE-SILENT: the exploit-phase
//           war-rate schedule (v2b; `escalate_mode`):
//             * mode 0 FIXED (CandE_fixed): the ORIGINAL fixed farming —
//               partner-adjusted r = r_star + partner_adj*(partner_warrate
//               - 0.3), clamped to [0.2, 0.55], every round;
//             * mode 1 STAGED (CandE_Hybrid, default): war-rate r(k) by
//               rounds-since-classify k = n - classify_end - 1:
//                 k <  escalate_stage2_rd  -> stage1 band: the
//                       partner_adj formula applies in THIS band only
//                       (base stage1_rate, clamped [0.2, 0.55]);
//                 k in [escalate_stage2_rd, escalate_commit_rd) -> the fixed
//                       stage2_rate (no partner adj);
//                 k >= escalate_commit_rd -> 1.0 (commit: pure war, no draw);
//             * mode 2 COMMIT (CandE_commit): pure war from classify_end+1
//               (immediate commit; k >= 0).
//           One iid fast_rng draw per scheduled round (0/1 short-circuit
//           without a draw; the 0.55/0.2 clamp applies in mode 0 and the
//           STAGED stage1 band only — stage2/commit bands are fixed).
//       In ALL modes: a single opp war is an immediate single-round war
//       response ("3/rd regime, never absorb") and the schedule STAYS ARMED
//       (it re-evaluates next round; the escalation never downgrades on a
//       late switch); >=2 opp wars within any flip_win-rd post-classify
//       window flips the pair to responsive handling (one-way, CandC
//       precedent — the flip-guard is identical across all three modes).
//   R6  UNCERTAIN default (classify window, no trigger active): peace (the
//       4-stream protection).
//
// Rule-ambiguity decisions (literal readings — flagged per dispatch):
//   1. R1 "my war-streak >=3" = my last 3 actions are all war (a longer
//      streak implies it; reading exactly 3 keeps it O(window)).
//   2. R1's "2 consecutive all-peace rds" reset is checked on the most
//      recent 2 rounds of BOTH opponent streams, at the start of each round
//      (before the re-commit test).
//   3. R2 keeps CandB's window=3/thresh=2 (the dispatch's CandEParams carries
//      no window/thresh coordinates, so the CandB defaults are the literal
//      reading); "2 wars" includes the trigger round (CandB bookkeeping).
//   4. R2 is a WINDOW-only phase (the pre-commit backbone). After the pair
//      is classified, the responsive policy is the clean memoryless TFT_any
//      + g=0.5 (the dispatch names exactly those, not the machine), so the
//      machine does not drive the responsive phase.
//   5. R5 "partner" = the JOINT partner war-rate: the AVERAGE of both
//      opponents' all-history war-rates. The 3-way game has no single
//      "paired" opponent slot, so the symmetric average is the operative
//      reading (for a genuinely silent pair both rates are ~0, so the
//      effective r lands near the 0.2 clamp floor for the default r_star).
//   6. R5 flip = >=2 war rounds by ONE opponent within any flip_win-rd
//      window post-classify (CandC's flip test, generalized to flip_win).
//   7. R1.5 "preProbeClean" = neither opponent stream contains a war
//      BEFORE the first round of my war-streak (my first war). The
//      Nasty-pair warred at rd 0 (before my first war) -> false -> the
//      rule can never fire vs them (hostility safety).
//   8. R1.5 echo pattern: "each opp's war in each of the last 2 rds
//      coincides with my war the prior rd" = for t in {n-2, n-1}:
//      o1[t] == my[t-1] && o2[t] == my[t-1]. Since the streak forces the
//      last 2 opp rounds to be all-war, this requires my[n-3] == my[n-2]
//      == 1 — the opponents COPY me (TFT echo), they do not start it.
//      The pattern is verified in the same call that checks the streak,
//      so a break only starts on a round where the full pattern holds.
//   9. R1.5 "suppressing R2 backbone trigger, R2-amnesty machinery" =
//      the machine is FROZEN (no phase-step, no leniency/jitter draws)
//      during a break and CLEARED (kNormal, rem_=0) on completion. After
//      an abort, the machine resumes as the standard kNormal machine
//      (a fresh R2 trigger draws its leniency bit; a >=2-war tail goes
//      straight to punish — the punish state is re-armed, as specified).
//      A break does NOT re-run R1's commit test (R1 stays suppressed for
//      the break's duration and is re-tested on later rounds only if
//      re-entry conditions hold — the frozen machine + kNormal resume
//      keeps the state space small).
//  10. R1.5 precedence vs R3: the break is checked first, but the break
//      action itself honors the endgame (a break round at n >=
//      endgame_responsive wars instead of peacing, aborting the break —
//      "R3 endgame still applies" is implemented as that override).
//  11. v2b "war from classify_end+1" (mode 2) and "k = rounds-since-
//      classify" (mode 1): the FIRST call with n >= classify_end+1 is the
//      classify call itself, so the post-classify exploit phase runs on
//      calls n = classify_end+1 .. endgame-1; k = n - (classify_end+1)
//      counts completed exploit-phase rounds (k=0 on the classify call).
//      Mode 2 therefore commits from the classify call itself (k >= 0).
//      The flip-guard window is unchanged ([classify_end+1, n), i.e. it
//      never looks back into the pre-classify window) in all three modes.
//  12. v2b "partner_adj formula applies in this band only" (mode 1,
//      stage1 band): the partner-adjusted rate is
//      r = stage1_rate + partner_adj*(partner_warrate - 0.3), clamped to
//      [0.2, 0.55] — i.e. the ORIGINAL R5 formula with stage1_rate
//      replacing r_star (r_star itself is ignored in mode 1). The stage2
//      and commit bands use fixed rates (no partner adj, no clamp needed:
//      stage2_rate and 1.0 are already in [0,1]); the 0/1 short-circuit
//      without a draw still applies (e.g. a stage2_rate of 0 or 1 is
//      deterministic).
//
// Conventions (sim/README.md §C1a/C1b + zoo/zoo_common.hpp):
//   * n == number of completed rounds; "rd k" is the 0-based index k.
//   * RNG: draws ONLY from the engine thread-local fast_rng(); degenerate
//     probabilities (0/1) short-circuit WITHOUT a draw (C1a precedent).
//     Draw order per round (fixed, so a given history + seed stream is
//     reproducible): the leniency draw (window, when 0<leniency_eps<1),
//     then the burst/trickle draw (R4) or the farm draw (R5 exploit), then
//     the responsive g=0.5 draw (R5).
//   * O(window) per round (windows are small constants: 3 / nasty_win /
//     flip_win / the 15-rd probe window); the few full-history scans (probe
//     pre-hostility, flip, partner warrate) run over the engine's flat
//     <=110-entry storage — the zoo's "stateless rescan" pattern (Grim_any).
//   * Match-local member state only; a fresh agent is constructed per match
//     by the engine, so state resets naturally per match.
//   * Registered via REGISTER_AGENT(CandE_Hybrid) with default params;
//     make_CandE_Hybrid(params) is the parameterized API.
// =============================================================================

#include "candidates_e.hpp"

#include "../zoo/zoo_common.hpp"  // fast_rng() (RNG contract)

#include <algorithm>
#include <vector>

namespace mas_sim {

namespace {

// Joint pair class.
enum class PairClass {
  kUncertain,     // still in the classify window (R6 default)
  kHopeless,      // any stream hopeless -> permanent war
  kResponsive,    // either stream responsive -> clean TFT_any + g=0.5
  kExploitSilent  // both silent -> partner-adjusted farming (+ flip/late-switch)
};

// R2 backbone phase machine (CandB bookkeeping; window-only).
enum class Phase { kNormal, kPunish, kAmnesty };

// Draw a single bit w.p. p with the 0/1 short-circuit (deterministic
// degenerate variants consume no stream value) — the C1a/C1b convention.
int draw_w_p(double p) {
  if (p <= 0.0) return 0;
  if (p >= 1.0) return 1;
  return (fast_rng().uniform() < p) ? 1 : 0;
}

// Count 1s (war) in the last `w` entries of h (fewer if shorter).
inline int war_count_last(const std::vector<int>& h, int w) {
  int c = 0;
  const int n = static_cast<int>(h.size());
  int lo = n - w;
  if (lo < 0) lo = 0;
  for (int i = lo; i < n; ++i) c += h[i];
  return c;
}

// Total war count of a full history (for the partner-adjusted warrate).
inline int war_count_all(const std::vector<int>& h) {
  int c = 0;
  for (int v : h) c += v;
  return c;
}

// R1: did BOTH opponents war on each of the last `need` rounds?
bool squeeze_holds(const std::vector<int>& o1, const std::vector<int>& o2,
                   int need) {
  const int n = static_cast<int>(o1.size());
  if (n < need) return false;
  for (int i = n - need; i < n; ++i)
    if (o1[i] != 1 || o2[i] != 1) return false;
  return true;
}

// R1 reset: did BOTH opponents play all-peace on each of the last `need`
// rounds?
bool both_peace(const std::vector<int>& o1, const std::vector<int>& o2,
                int need) {
  const int n = static_cast<int>(o1.size());
  if (n < need) return false;
  for (int i = n - need; i < n; ++i)
    if (o1[i] != 0 || o2[i] != 0) return false;
  return true;
}

// R2 trigger strength: the max, over the two streams, of wars in the last
// 3 rounds.  >=2 -> full threshold (punish, no forgiveness); ==1 -> exactly
// one war total (isolated, forgiven w.p. leniency_eps); 0 -> normal base.
inline int backbone_wars(const std::vector<int>& o1,
                         const std::vector<int>& o2) {
  return std::max(war_count_last(o1, 3), war_count_last(o2, 3));
}

// R1.5 preProbeClean: NEITHER opponent stream warred before my first war.
// The first war index is the start of my leading-0 run.
bool pre_probe_clean(const std::vector<int>& my,
                     const std::vector<int>& o1,
                     const std::vector<int>& o2) {
  int first = 0;
  const int mn = static_cast<int>(my.size());
  while (first < mn && my[first] == 0) ++first;
  const int lim1 = std::min(static_cast<int>(o1.size()), first);
  const int lim2 = std::min(static_cast<int>(o2.size()), first);
  for (int i = 0; i < lim1; ++i)
    if (o1[i] != 0) return false;
  for (int i = 0; i < lim2; ++i)
    if (o2[i] != 0) return false;
  return true;
}

// R1.5: the pair's consecutive both-opp war-streak at the TAIL (both
// opponents war on each of the last `need` rounds).
bool both_war_streak(const std::vector<int>& o1,
                     const std::vector<int>& o2, int need) {
  return squeeze_holds(o1, o2, need);
}

// R1.5 copying-echo pattern: for t in {n-2, n-1}, each opponent's war
// exactly coincides with MY war the round before (they copy me). Requires
// n >= 3. The caller also checks the streak (the opps' last 2 are wars),
// so this reduces to my[n-3] == my[n-2] == 1.
bool echo_pattern(const std::vector<int>& my,
                  const std::vector<int>& o1,
                  const std::vector<int>& o2) {
  const int n = static_cast<int>(o1.size());
  if (n < 3) return false;
  for (int t = n - 2; t < n; ++t) {
    const int m = (t - 1 >= 0) ? my[t - 1] : 0;
    if (o1[t] != m || o2[t] != m) return false;
  }
  return true;
}

// R4: has any opponent war been observed in the whole history so far (the
// "retaliation observed" test that aborts the burst)?
bool any_opp_war_sofar(const std::vector<int>& o1,
                       const std::vector<int>& o2) {
  return war_count_all(o1) != 0 || war_count_all(o2) != 0;
}

// R4: war in rounds 0..probe_rd-1 (the "already-hostile" pre-probe test).
bool hostile_pre_probe(const std::vector<int>& o1,
                       const std::vector<int>& o2, int probe_rd) {
  const int lim = std::min(static_cast<int>(o1.size()), probe_rd);
  for (int i = 0; i < lim; ++i)
    if (o1[i] != 0 || o2[i] != 0) return true;
  return false;
}

// R5 hopeless test (CandC): the stream's last nasty_win-rd war-rate >=
// nasty_thresh.
bool is_hopeless(const std::vector<int>& h, int n, const CandEParams& p) {
  const int w = (n < p.nasty_win) ? n : p.nasty_win;
  if (w <= 0) return false;
  const int c = war_count_last(h, p.nasty_win);  // clamps internally
  return static_cast<double>(c) / static_cast<double>(w) >= p.nasty_thresh;
}

// R5 retaliation test (CandC): warred on probe_rd, or on >=2 burst rounds.
bool retaliated(const std::vector<int>& h, int n, const CandEParams& p) {
  if (n > p.probe_rd && h[p.probe_rd] == 1) return true;
  int b = 0;
  const int hi = (n < p.burst_start + p.burst_len) ? n
                                                   : p.burst_start + p.burst_len;
  for (int i = p.burst_start; i < hi; ++i)
    if (i >= 0) b += h[i];
  return b >= 2;
}

// R5 flip detector: >=2 war rounds by this stream within any flip_win-rd
// window in the post-classify region [classify_end+1, n). (CandC's
// flip_trigger, generalized to flip_win: flip_win consecutive rounds hold
// two indices iff their distance is <= flip_win-1.)
bool flip_trigger(const std::vector<int>& h, int n, const CandEParams& p) {
  const int lo = p.classify_end + 1;
  if (n - 1 < lo) return false;
  for (int i = lo; i < n; ++i) {
    if (h[i] != 1) continue;
    const int hi = (i + p.flip_win < n) ? i + p.flip_win : n;
    for (int j = i + 1; j < hi; ++j)
      if (h[j] == 1) return true;
  }
  return false;
}

// R5 responsive handling: TFT_any with one-shot forgiveness of an isolated
// single-opp offense at fixed g = 0.5 (exactly CandA_GTFT / CandC's
// responsive_decision). "Isolated" = that opp had zero wars in the 3 rounds
// before the last (indices [n-4, n-1), clamped).
int responsive_decision(int n, const std::vector<int>& o1,
                        const std::vector<int>& o2) {
  if (n == 0) return 0;
  const int w1 = o1[n - 1];
  const int w2 = o2[n - 1];
  if (w1 && w2) return 1;  // both warred -> always war
  if (!w1 && !w2) return 0;
  const std::vector<int>& o = w1 ? o1 : o2;
  int lo = n - 4;
  if (lo < 0) lo = 0;
  int prev_wars = 0;
  for (int i = lo; i < n - 1; ++i) prev_wars += o[i];
  if (prev_wars != 0) return 1;  // non-isolated -> war
  // Isolated -> one-shot forgiveness at g = 0.5 (0.5 is never 0/1, so this
  // always draws).
  return (fast_rng().uniform() < 0.5) ? 0 : 1;
}

}  // namespace

// =============================================================================
// Candidate E — CandE_Hybrid
// =============================================================================
class CandEHybrid : public AgentBase {
  CandEParams p_;

  // R2 backbone phase machine (CandB bookkeeping: `rem_` counts the phase
  // rounds remaining AFTER the current call). Window-only (R2 active only
  // pre-commit); the machine is never consulted once the pair is classified
  // (flagged reading 4).
  Phase phase_ = Phase::kNormal;
  int rem_ = 0;

  // R1 anti-lock commit (match-local; lifted only by the 2-all-peace reset).
  bool squeeze_commit_ = false;

  // R1.5 feud-break (contrite de-escalation). break_rem_ > 0 means a break
  // is in flight (feud_break_in_progress decides the round; the machine is
  // frozen, then cleared on completion/abort). pre_probe_clean_ is
  // match-local, evaluated once at the first streak+echo fire: "neither
  // opponent warred before my first war this match". It stays as computed
  // after a completed break, so a recurring echo re-enters the break; when
  // false (e.g. the Nasty-pair that warred at rd 0) the rule never fires.
  int break_rem_ = 0;
  bool pre_probe_clean_ = false;
  bool pp_evaluated_ = false;

  // R5 joint class + one-way flip (exploit -> responsive).
  PairClass pair_ = PairClass::kUncertain;
  bool flipped_ = false;

  // My war-streak: >=3 iff my last 3 actions are all war (flagged reading 1).
  static bool my_streak_ge3(const std::vector<int>& my) {
    const int n = static_cast<int>(my.size());
    if (n < 3) return false;
    return my[n - 1] != 0 && my[n - 2] != 0 && my[n - 3] != 0;
  }

  // Draw the amnesty length: amnesty_len +/- jitter, one fast_rng draw spread
  // over (2*jitter+1) equal buckets, clamped >= 1.
  int draw_amnesty_len() {
    if (p_.jitter <= 0) {
      int len = p_.amnesty_len;
      return (len < 1) ? 1 : len;
    }
    const double u = fast_rng().uniform();
    const int range = 2 * p_.jitter + 1;
    int v = static_cast<int>(u * static_cast<double>(range));
    if (v >= range) v = range - 1;  // uniform() < 1, defensive clamp
    int len = p_.amnesty_len + (v - p_.jitter);  // -jitter .. +jitter
    return (len < 1) ? 1 : len;
  }

  // R2 backbone step (window-only). Returns the machine's action, or -1 when
  // the machine is in kNormal with no trigger (the caller falls through to
  // the R4 script). The leniency draw happens BEFORE counting (CandB).
  int backbone_step(const std::vector<int>& o1, const std::vector<int>& o2) {
    if (phase_ == Phase::kPunish) {
      if (rem_ > 0) {
        --rem_;
        return 1;
      }
      // Punish done -> enter amnesty; this round is the first peace.
      const int len = draw_amnesty_len();
      phase_ = Phase::kAmnesty;
      rem_ = len - 1;
      return 0;
    }
    if (phase_ == Phase::kAmnesty) {
      --rem_;
      if (rem_ <= 0) {
        phase_ = Phase::kNormal;
        rem_ = 0;
      }
      return 0;
    }
    // kNormal: the leniency draw is made BEFORE counting (CandB).
    const bool strict = p_.leniency_eps > 0.0 && p_.leniency_eps < 1.0;
    const double u = strict ? fast_rng().uniform() : 0.0;
    const int w = backbone_wars(o1, o2);
    if (w >= 2) {
      phase_ = Phase::kPunish;
      rem_ = (p_.punish_len > 1) ? p_.punish_len - 1 : 0;
      return 1;  // full threshold: punish, no forgiveness
    }
    if (w == 1) {
      const bool forgive = (p_.leniency_eps >= 1.0)
                              ? true
                              : (p_.leniency_eps <= 0.0)
                                    ? false
                                    : (u < p_.leniency_eps);
      if (forgive) return 0;  // isolated, forgiven
      phase_ = Phase::kPunish;
      rem_ = (p_.punish_len > 1) ? p_.punish_len - 1 : 0;
      return 1;  // isolated, not forgiven: punish
    }
    return -1;  // no trigger -> caller's R4 script
  }

  // R4 conditional probe script (the base of a normal window round; called
  // only when no backbone phase/trigger is active).
  int probe_script(int n, const std::vector<int>& o1,
                   const std::vector<int>& o2) {
    if (n == p_.probe_rd) {
      // Conditional probe: only vs a pair that has not warred in the
      // pre-probe window (0..probe_rd-1); skip when already hostile.
      return hostile_pre_probe(o1, o2, p_.probe_rd) ? 0 : 1;
    }
    if (n >= p_.burst_start && n < p_.burst_start + p_.burst_len) {
      // Burst: only if zero retaliation observed so far; any opp war in the
      // window aborts the burst immediately (skip -> peace, no draw).
      if (any_opp_war_sofar(o1, o2)) return 0;
      return draw_w_p(p_.burst_rate);
    }
    if (n > p_.probe_rd && n < p_.burst_start + p_.burst_len) {
      // Trickle on the other in-window rounds (unconditional draw).
      return draw_w_p(p_.trickle_rate);
    }
    return 0;  // R6 uncertain default: peace
  }

  // R2/R4/R6: one round inside the classification window (the machine
  // supersedes the script on conflict).
  int window_round(int n, const std::vector<int>& o1,
                   const std::vector<int>& o2) {
    const int m = backbone_step(o1, o2);
    if (m >= 0) return m;  // punish war / amnesty peace
    return probe_script(n, o1, o2);  // no phase/trigger -> the R4 script
  }

  // R1.5: a break IN FLIGHT decides the round. Returns -1 when no break is
  // in flight (none this round); 0 for the unilateral peace (the machine
  // stays frozen — R2/amnesty suppressed); 1 for the R3 endgame override (a
  // break round at n >= endgame_responsive wars; break aborted, machine
  // cleared — "R3 endgame still applies"); or the ABORT sentinel (-2) for a
  // mid-break abort: an opponent wars this round while my prior action was
  // peace (genuine hostility, not an echo of my war) -> break_rem_ cleared;
  // selectAction uses the sentinel to suppress the entry gate for just this
  // round so the normal machine (R1/R2, punish re-armed) resumes instead of
  // immediately re-firing a new break on the stale echo pattern. On
  // completion (break_rem_ -> 0) the punish/amnesty state is cleared so the
  // R6 peace default resumes; pre_probe_clean_ stays as evaluated
  // (recurring echoes re-enter the break).
  int feud_break_in_progress(int n, const std::vector<int>& my,
                             const std::vector<int>& o1,
                             const std::vector<int>& o2) {
    if (break_rem_ <= 0) return -1;  // no break in flight
    if (n >= p_.endgame_responsive) {
      break_rem_ = 0;  // R3 endgame: war; break aborted
      phase_ = Phase::kNormal;
      rem_ = 0;
      return 1;
    }
    if ((o1[n - 1] != 0 || o2[n - 1] != 0) && my[n - 1] == 0) {
      break_rem_ = 0;  // mid-break abort: resume the machine below
      return -2;  // ABORT sentinel
    }
    --break_rem_;
    if (break_rem_ == 0) {  // break complete: clear the machine
      phase_ = Phase::kNormal;
      rem_ = 0;
    }
    return 0;  // unilateral peace (machine frozen)
  }

  // R1.5 ENTRY (the classification-window phase, between R1 and R2).
  // Fires when the both-opp war-streak is >= feud_min_streak AND the
  // copying-echo pattern holds AND the pre-probe phase was clean. Returns
  // -1 (no fire) or 0 (the break starts this round — R1 squeeze-commit and
  // the R2 backbone/amnesty are suppressed for the break's duration), or
  // 1 (the R3 endgame overrides a would-be break with a war).
  int feud_break_entry(int n, const std::vector<int>& my,
                       const std::vector<int>& o1,
                       const std::vector<int>& o2) {
    if (!both_war_streak(o1, o2, p_.feud_min_streak)) return -1;
    if (!echo_pattern(my, o1, o2)) return -1;
    if (!pp_evaluated_) {
      // First (and only) evaluation of the clean gate: it is match-local
      // and stays as computed (true -> a recurring echo re-enters the
      // break; false -> the rule can never fire, e.g. the Nasty-pair that
      // warred at rd 0 before my first war).
      pre_probe_clean_ = pre_probe_clean(my, o1, o2);
      pp_evaluated_ = true;
    }
    if (!pre_probe_clean_) return -1;  // hostile pre-probe: never fire
    if (n >= p_.endgame_responsive) return 1;  // R3 overrides the break
    break_rem_ = p_.feud_break_len - 1;  // this round is the first peace
    return 0;
  }

  // R5: one round in the exploit (both-silent) phase. Precedence: R3
  // endgame (top, suppresses the schedule); the flip-guard (one-way
  // downgrade to responsive); the late-switch single-response (the
  // escalation schedule stays ARMED — it re-evaluates next round); then the
  // mode-specific war-rate (the v2b escalation schedule).
  int exploit_round(int n, const std::vector<int>& o1,
                    const std::vector<int>& o2) {
    // R3 endgame: exploitable-silent -> pure war (schedule suppressed).
    if (n >= p_.endgame_exploit) return 1;

    // (b) flip-guard: >=2 wars by an opp in any flip_win-rd post-classify
    // window -> one-way downgrade to responsive handling (ALL modes).
    if (!flipped_ &&
        (flip_trigger(o1, n, p_) || flip_trigger(o2, n, p_)))
      flipped_ = true;
    if (flipped_) return responsive_decision(n, o1, o2);

    // (e) late-switch guard: a single opp war -> immediate single-round war
    // response (3/rd regime, never absorbed). The schedule STAYS ARMED: the
    // escalation clock keeps running and re-evaluates next round (it never
    // downgrades on a late switch) — identical across all three modes.
    if (o1[n - 1] != 0 || o2[n - 1] != 0) return 1;

    // (a) the mode-specific war-rate for this round (v2b escalation).
    // mode 2 COMMIT: pure war from classify_end+1 (immediate commit).
    if (p_.escalate_mode == 2) return 1;

    // mode 1 STAGED: war-rate by rounds-since-classify
    // k = n - (classify_end + 1) (flag 11; k = 0 on the classify call).
    if (p_.escalate_mode == 1) {
      const int k = n - (p_.classify_end + 1);
      if (k >= p_.escalate_commit_rd) return 1;  // commit band: pure war
      if (k >= p_.escalate_stage2_rd)
        return draw_w_p(p_.stage2_rate);  // stage2 band: fixed rate
      // stage1 band: partner_adj formula (base stage1_rate, clamp [0.2,0.55])
      const double wr1 =
          (n > 0) ? static_cast<double>(war_count_all(o1)) / n : 0.0;
      const double wr2 =
          (n > 0) ? static_cast<double>(war_count_all(o2)) / n : 0.0;
      const double wr = 0.5 * (wr1 + wr2);
      double r = p_.stage1_rate + p_.partner_adj * (wr - 0.3);
      if (r < 0.2) r = 0.2;
      if (r > 0.55) r = 0.55;
      return draw_w_p(r);
    }

    // mode 0 FIXED: the ORIGINAL partner-adjusted farming — r = r_star +
    // partner_adj*(wr - 0.3), clamped [0.2,0.55]; partner warrate = the
    // average of both opponents' all-history war-rates (flagged reading 5).
    const double wr1 =
        (n > 0) ? static_cast<double>(war_count_all(o1)) / n : 0.0;
    const double wr2 =
        (n > 0) ? static_cast<double>(war_count_all(o2)) / n : 0.0;
    const double wr = 0.5 * (wr1 + wr2);
    double r = p_.r_star + p_.partner_adj * (wr - 0.3);
    if (r < 0.2) r = 0.2;
    if (r > 0.55) r = 0.55;
    return draw_w_p(r);
  }

  // R5: joint class from the per-stream two-test classifier.
  PairClass classify_pair(const std::vector<int>& o1,
                          const std::vector<int>& o2, int n) {
    const bool h1 = is_hopeless(o1, n, p_);
    const bool h2 = is_hopeless(o2, n, p_);
    if (h1 || h2) return PairClass::kHopeless;
    const bool r1 = retaliated(o1, n, p_);
    const bool r2 = retaliated(o2, n, p_);
    if (r1 || r2) return PairClass::kResponsive;
    return PairClass::kExploitSilent;
  }

 public:
  explicit CandEHybrid(const CandEParams& p) : p_(p) {}

  int selectAction(int n, std::vector<int>& my, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    // R0: open peace.
    if (n == 0) return 0;

    // R1.5 FEUD-BREAK — precedence sits BETWEEN R1 and R2. (a) A break in
    // flight decides the round (unilateral peace, R3 endgame override, or
    // mid-break abort); it suppresses R1 and the R2 backbone/amnesty.
    //   in_flight == 0  -> unilateral peace (return).
    //   in_flight == 1  -> R3 endgame override (return).
    //   in_flight == -2 -> mid-break ABORT: the break is over; suppress the
    //                      entry gate for THIS round (the stale echo would
    //                      otherwise re-fire a new break) and resume the
    //                      normal machine (R1/R2, punish re-armed).
    //   in_flight == -1 -> no break in flight.
    const int in_flight = feud_break_in_progress(n, my, o1, o2);
    if (in_flight >= 0) return in_flight;
    const bool aborted_this_round = (in_flight == -2);
    // (b) Entry gate: a fresh (not in flight, and not a just-aborted) break
    // starts here, when the both-war streak + copying-echo + clean pre-probe
    // hold and we are in the classification window (R1.5 is a window-phase
    // rule, like R1/R2).
    if (!aborted_this_round && pair_ == PairClass::kUncertain &&
        n < p_.classify_end + 1) {
      const int entry = feud_break_entry(n, my, o1, o2);
      if (entry >= 0) return entry;  // 0: start break (peace); 1: R3 endgame
    }

    // R1: reset first (two consecutive all-peace rounds), then the re-commit
    // test (both opps war on each of the last >=2 rounds AND my streak>=3).
    if (squeeze_commit_ && both_peace(o1, o2, 2)) squeeze_commit_ = false;
    if (!squeeze_commit_ && squeeze_holds(o1, o2, 2) && my_streak_ge3(my))
      squeeze_commit_ = true;
    if (squeeze_commit_) return 1;  // permanent war until the reset above

    // Classify-window phase (R2/R4/R6) vs post-classify phase (R3/R5).
    if (pair_ == PairClass::kUncertain) {
      if (n < p_.classify_end + 1) return window_round(n, o1, o2);
      pair_ = classify_pair(o1, o2, n);
    }

    switch (pair_) {
      case PairClass::kHopeless:
        return 1;  // (d) permanent war
      case PairClass::kResponsive:
        if (n >= p_.endgame_responsive) return 1;  // R3 (unclean endgame)
        return responsive_decision(n, o1, o2);     // (c) clean TFT_any + g=0.5
      case PairClass::kExploitSilent:
        return exploit_round(n, o1, o2);  // R3 checked first inside
      default:
        break;
    }
    return 0;
  }
};

// =============================================================================
// Factories
// =============================================================================

AgentFactory make_CandE_Hybrid(const CandEParams& p) {
  return [p] { return std::make_unique<CandEHybrid>(p); };
}

// Registry-facing factory: default-parameter instance (mode 1 STAGED).
AgentFactory make_agent_CandE_Hybrid() {
  return make_CandE_Hybrid();
}

// v2b named variant: mode 0 FIXED (the original fixed clamp-rate farming).
AgentFactory make_CandE_fixed() {
  CandEParams p;
  p.escalate_mode = 0;
  return make_CandE_Hybrid(p);
}

// v2b named variant: mode 2 COMMIT (pure war from classify_end+1).
AgentFactory make_CandE_commit() {
  CandEParams p;
  p.escalate_mode = 2;
  return make_CandE_Hybrid(p);
}

// Registry-facing factories for the v2b named variants.
AgentFactory make_agent_CandE_fixed() { return make_CandE_fixed(); }
AgentFactory make_agent_CandE_commit() { return make_CandE_commit(); }

// Link-time registration (names are the literal dispatch tokens).
REGISTER_AGENT(CandE_Hybrid);
REGISTER_AGENT(CandE_fixed);
REGISTER_AGENT(CandE_commit);

}  // namespace mas_sim
