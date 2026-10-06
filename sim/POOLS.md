# Campaign Protocol — Pool Compositions, Run Budgets, Hold-Out Discipline

Binding methodology for Phase C. Grounded in `research/LITERATURE.md` §5 (stratified Monte-Carlo,
hold-out compositions, CRN, adversarial augmentation) and §6 (classmate prior).

## 1. Composition families

Zoo names refer to registered agents (see `sim/zoo/`). `count` semantics per `campaign --pool`.

| Family | Composition sketch (counts out of 100) | Role |
|---|---|---|
| `prior` | calibrated classmate prior: TFT-family ≈25 (TFT_any 8, TFT_maj 5, BrokenTFT_1 4, Joss10 4, TF2T_any 4), Tolerant-family ≈15 (TolerantAgent 5, TolerantW5 5, TolerantW10 5), Random ≈12 (RandomAgent 6, NoisyP20 3, NoisyP80 3), saints ≈8 (NiceAgent 5, Freaky25 3), nasty ≈4, grim ≈8, Freaky75 3, alternators/periodic ≈6, probers ≈4, strong ≈7 (Gradual-3P, CTFT-3P, k-lenient×2, classifier-hybrid×2, endgame-opt) | the plausible field |
| `nice-heavy` | saints+tolerant+TFT ≈ 60% | exploitation upside test |
| `retal-heavy` | grim+TFT_maj+anti-exploit ≈ 55% | cascade-risk test |
| `exploit-heavy` | probers/detective/ZD/saint-hunters ≈ 25%, rest prior mix | rivalry test |
| `elite-heavy` | Tier 3 + GA adversaries + copies of our best ≈ 20% | displacement test |
| `uniform-zoo` | 1–2 copies of every registered bot, remainder round-robin | coverage test |
| `llm-pattern` | LLM-paste archetypes only (Peaceful-TFT-forgiving, majority-vote, Joss, grim, lookup) | mode-of-field test |
| `small-pool` | same as `prior` but 50 agents | pool-size robustness (both runner interpretations) |

Hold-out discipline: parameter tuning runs on {`prior`, `nice-heavy`, `exploit-heavy`};
FINAL validation on fresh seeds + {`retal-heavy`, `elite-heavy`, `uniform-zoo`, `llm-pattern`,
`small-pool`} — compositions never used for tuning. Reported headline = `prior` P(#1);
guardrail = no composition where the ship candidate falls below baseline GTFT arm.

## 2. Run budgets (16-logical-core host, ~13 t/s/core engine)

- Screening arms A–E: `prior` × 5,000 runs each (CRN: identical seed streams across arms).
- Tuning grids: per-parameter-coordinate sweeps on `prior` × 2,000 CRN pairs; local search until
  marginal P(#1) gain < 1 CI width.
- Composition matrix (finalists): 8 families × 10,000 runs, CRN-paired across finalists.
- Adversarial self-play rounds: each round = GA generation (pop 64, 3 eval runs each vs incumbent
  pool) → top adversaries promoted to Tier 3 → re-tune; stop when a full generation fails to
  produce an adversary that displaces the incumbent's P(#1) by > 1 CI width.
- Final freeze validation: ship candidate vs strongest rival on all 8 families × 20,000 fresh
  seeds (≈ 160k runs) + the ×25,000-style rerun stability report on `prior`.
- Total ≈ 5–8 × 10⁵ runs ≈ 1.5–3 h wall-clock at measured throughput. Overnight budget absorbs
  reruns and the GA loop.

## 3. Metrics & decision rules

- Primary: **P(rank #1)** = fraction of runs where our agent's total is strictly max
  (ties: count as not-#1 — the real runner's insertion sort gives ties to the earlier-indexed
  agent, and we cannot rely on index luck; conservative).
- Secondary: mean rank, P(top-3), worst composition rank (maximin guardrail), per-archetype
  per-match contribution decomposition (identifies which exploitation layer pays).
- Selection rule: maximize P(#1) on `prior` subject to (a) ≥ baseline-GTFT arm on EVERY family,
  (b) no single Tier-3 adversary displacing it (self-play fixed point), (c) hold-out families
  confirm (fresh seeds).
- CI: Wilson intervals on P(#1); comparisons via CRN-paired differences (paired rank indicators).

## 4. Candidate arms (implemented in `sim/candidates/`)

A — GTFT-3P baseline (control). B — k-lenient probabilistic retaliator. C — pure two-test
classifier+exploiter. E — hybrid ship candidate (B-backbone + C-classifier + anti-lock +
dithering + endgame ramp). D (WSLS-3P) pre-rejected (see `research/GAME-ANALYSIS.md` §5) —
revisit only if the campaign contradicts the kill argument.

Parameter vector for E (tuning space, initial values from design synthesis):
probe_round_1 ∈ {3..6} (default 4), probe_round_2 ∈ {9..13} (default 11), burst_rate ∈ {0.5,0.6,0.7},
trickle_rate ∈ {0.05,0.1,0.15}, r_star ∈ {0.30,0.35,0.40,0.45} × partner adjustment ∈ {0,±0.05,±0.1},
leniency_eps ∈ {0.1,0.2,0.3}, amnesty ∈ {2,3,4} ± jitter 1, endgame_start per class ∈ {88..108},
saint_commit_threshold (confirm evidence count) ∈ {1,2}.

## 5. Adversarial self-play loop

GA over parameter vectors of Tier-2 archetype templates (and small 8-entry policy tables keyed on
(own-last, opps-wars-last, phase)) maximizing the generated agent's P(rank #1) in a pool =
incumbent + prior mix. Promote any adversary beating the incumbent head-to-head by > 2 points/match
average into Tier 3. Fixed point = no generation-64 member achieves positive head-to-head edge.

## 6. Deliverable artifacts

`sim/results/<family>/<arm>/results.jsonl` (raw), `sim/results/summary.json` (per-arm metrics +
CIs), `sim/results/final/` (frozen validation), and the campaign section of `REPORT.md`
(decision chain: which parameter choices the numbers justified).
