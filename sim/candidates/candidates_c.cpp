// =============================================================================
// sim/candidates/candidates_c.cpp — slice C1b: candidate C "CandC_Classifier"
//
// Single concern: the pure two-test classifier+exploiter candidate + its
// registration. NO hybrid/E, NO campaign runs (later slices).
//
//   * CandC_Classifier — pure exploitation arm: NO k-lenient backbone, NO
//     anti-lock, NO dithering beyond probe stochasticity.
//
// State machine (per match; a fresh agent instance is constructed per match
// by the engine, so state resets naturally):
//   probe:   n == 0 -> peace. Rounds 0..classify_end: peace EXCEPT war at
//            probe_rd (guaranteed); war at burst rounds
//            [burst_start, burst_start+burst_len) w.p. burst_rate (drawn);
//            w.p. trickle_rate on the other rounds in
//            [probe_rd+1, burst_start+burst_len); everything else peace.
//   classify (first call with n >= classify_end+1), per opponent stream:
//            (a) hopeless  — war-rate over its last nasty_win rounds
//                            >= nasty_thresh;
//            (b) responsive — it warred on probe_rd or >= 2 burst rounds
//                            (retaliated the probe);
//            (c) else silent (tentatively exploitable).
//            Joint refinement: either hopeless -> pair HOPEDLESS
//            (permanent war: respond 3/0, never -1, never -1: war every
//            round, no draws); else either responsive -> pair RESPONSIVE
//            (TFT_any from here: war iff either opp warred last rd, with
//            one-shot forgiveness of an ISOLATED single-opp war at g=0.5,
//            exactly like CandA_GTFT); else both silent -> pair EXPLOIT.
//   exploit: farm war-pattern w.p. r_star per round (iid fast_rng draw)
//            UNLESS
//            (i)  either opp has >= 2 war rounds within any 5-round window
//                 post-classification -> flip: pair reclassifies
//                 RESPONSIVE (upgrade only; never downgrades again);
//            (ii) endgame: rd (== n) >= 90 -> pure war (ceiling harvest:
//                 payoff[1][0][0]=6 vs silent opponents, who by
//                 classification never retaliate).
//   hopeless/responsive never downgrade during the match.
//
// Conventions (sim/README.md §B2/C1a + zoo/zoo_common.hpp):
//   * n == number of completed rounds (0-based round index for "rd k");
//     every bot has an n==0 guard and opens peace.
//   * RNG: draws ONLY from the engine thread-local fast_rng(); degenerate
//     probabilities (0/1) short-circuit WITHOUT a draw, making the
//     rate/r*==0/1 variants fully deterministic (C1a precedent, flagged).
//   * Registered via REGISTER_AGENT(CandC_Classifier) with default params;
//     make_CandC_Classifier(params) is the parameterized API.
// =============================================================================

#include "candidates_c.hpp"

#include "../zoo/zoo_common.hpp"  // fast_rng() (RNG contract)

namespace mas_sim {

namespace {

// Per-opp classification of its stream (0-based indices 0..n-1).
enum class OppClass { kSilent, kResponsive, kHopeless };

// War-rate test: hopeless if the fraction of wars in the opponent's last
// min(n, nasty_win) rounds is >= nasty_thresh.
bool is_hopeless(const std::vector<int>& h, int n, const CandCParams& p) {
  const int w = (n < p.nasty_win) ? n : p.nasty_win;
  if (w <= 0) return false;
  int c = 0;
  for (int i = n - w; i < n; ++i) c += h[i];
  return static_cast<double>(c) / static_cast<double>(w) >= p.nasty_thresh;
}

// Retaliation test: warred on probe_rd, or on >= 2 burst rounds.
bool retaliated(const std::vector<int>& h, int n, const CandCParams& p) {
  if (n > p.probe_rd && h[p.probe_rd] == 1) return true;
  int b = 0;
  const int hi = (n < p.burst_start + p.burst_len) ? n
                                                   : p.burst_start + p.burst_len;
  for (int i = p.burst_start; i < hi; ++i)
    if (i >= 0) b += h[i];
  return b >= 2;
}

OppClass classify_opp(const std::vector<int>& h, int n, const CandCParams& p) {
  if (is_hopeless(h, n, p)) return OppClass::kHopeless;
  if (retaliated(h, n, p)) return OppClass::kResponsive;
  return OppClass::kSilent;
}

// Flip test (exploit phase, rule i): did the opp have >= 2 war rounds within
// any 5-round window post-classification?  5 consecutive rounds contain two
// indices iff their distance is <= 4; the post-classify region is
// [classify_end+1, n).
bool flip_trigger(const std::vector<int>& h, int n, const CandCParams& p) {
  const int lo = p.classify_end + 1;
  if (n - 1 < lo) return false;
  for (int i = lo; i < n; ++i) {
    if (h[i] != 1) continue;
    const int hi = (i + 5 < n) ? i + 5 : n;  // window [i, i+5) clamped
    for (int j = i + 1; j < hi; ++j)
      if (h[j] == 1) return true;
  }
  return false;
}

// Responsive-phase decision: TFT_any with one-shot forgiveness of an
// isolated single-opp offense at fixed g = 0.5 (exactly CandA_GTFT's
// structure, g pinned).  "Isolated" = that opp had zero wars in the 3
// rounds before the last (indices [n-4, n-1), clamped) — same 0-based
// reading as CandA (README §C1a decision 2).
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
  // Isolated -> one-shot forgiveness at g = 0.5 (g is neither 0 nor 1,
  // so this always draws).
  return (fast_rng().uniform() < 0.5) ? 0 : 1;
}

// Draw w.p. p with 0/1 short-circuit (deterministic degenerate variants).
int draw_w_p(double p) {
  if (p <= 0.0) return 0;
  if (p >= 1.0) return 1;
  return (fast_rng().uniform() < p) ? 1 : 0;
}

}  // namespace

// =============================================================================
// Candidate C — CandC_Classifier
// =============================================================================
class CandCClassifier : public AgentBase {
  CandCParams p_;
  int phase_ = 0;  // 0 = probe, 1 = hopeless, 2 = responsive, 3 = exploit

 public:
  explicit CandCClassifier(const CandCParams& p) : p_(p) {}

  int selectAction(int n, std::vector<int>&, std::vector<int>& o1,
                   std::vector<int>& o2) override {
    if (n == 0) return 0;  // open peace (all phases)

    // Probe script: rounds 0..classify_end.
    if (phase_ == 0 && n <= p_.classify_end) {
      if (n == p_.probe_rd) return 1;  // guaranteed probe war
      if (n >= p_.burst_start && n < p_.burst_start + p_.burst_len)
        return draw_w_p(p_.burst_rate);  // burst war, drawn (non-periodic)
      if (n > p_.probe_rd && n < p_.burst_start + p_.burst_len)
        return draw_w_p(p_.trickle_rate);  // trickle, drawn
      return 0;
    }

    // First call with n >= classify_end+1 -> joint classification.
    if (phase_ == 0) {
      if (n < p_.classify_end + 1) return 0;  // (defensive; unreachable when
                                              // n <= classify_end handled above)
      const OppClass c1 = classify_opp(o1, n, p_);
      const OppClass c2 = classify_opp(o2, n, p_);
      if (c1 == OppClass::kHopeless || c2 == OppClass::kHopeless) {
        phase_ = 1;  // hopeless: permanent war (never downgrades)
        return 1;
      }
      if (c1 == OppClass::kResponsive || c2 == OppClass::kResponsive) {
        phase_ = 2;  // responsive: TFT_any from here
        return responsive_decision(n, o1, o2);
      }
      phase_ = 3;  // both silent -> exploit
    }

    if (phase_ == 1) return 1;  // hopeless: permanent war, 3/0, no draws

    if (phase_ == 2) return responsive_decision(n, o1, o2);

    // Exploit phase.
    if (flip_trigger(o1, n, p_) || flip_trigger(o2, n, p_)) {
      phase_ = 2;  // rule (i) flip: upgrade to responsive (one-way)
      return responsive_decision(n, o1, o2);
    }
    if (n >= 90) return 1;  // endgame: pure war (ceiling harvest vs silent)
    return draw_w_p(p_.r_star);  // farm war-pattern w.p. r_star (iid)
  }
};

// =============================================================================
// Factories
// =============================================================================

AgentFactory make_CandC_Classifier(const CandCParams& p) {
  return [p] { return std::make_unique<CandCClassifier>(p); };
}

// Registry-facing factory: default-parameter instance.
AgentFactory make_agent_CandC_Classifier() {
  return make_CandC_Classifier();
}

// Link-time registration (name is the literal dispatch token).
REGISTER_AGENT(CandC_Classifier);

}  // namespace mas_sim
