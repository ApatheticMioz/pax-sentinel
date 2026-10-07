# Game Analysis & Design Rationale — Three-Way Peace-War

The design-rationale chain: every shipped rule maps to (a) a literature citation
(`LITERATURE.md`), (b) a campaign number (`sim/results/`), or (c) a ground-truth constraint.

## 1. Ground truth (from code)

3-player simultaneous repeated game, actions Peace(0)/War(1). A player's payoff depends only on
their own action and the number of War players among the other two:

| My action | 0 opp wars | 1 opp war | 2 opp wars |
|---|---|---|---|
| Peace | **4** | **1** | **−1** |
| War | **6** | **3** | **0** |

- `payoff[A][B][C] = {{{4,1},{1,-1}},{{6,3},{3,0}}}`; runner reads cyclically.
- Horizon per match: `rounds = 90 + round(20*(float)rand()/RAND_MAX)` → uniform 90–110.
- Match score contributed = `matchResults[i] / rounds` (exact types per source), summed over matches.
- Per-match fresh construction (no cross-match state, no opponent identity, full in-match
  history visible); shared `rand()` seeded once (`srand(time(NULL))`).
- Sample pool (what ~99 classmates will copy/vary): Nice, Nasty, Random, Tolerant
  (cumulative majority), Freaky (per-match constant), T4T (copies a random opponent's last move).

## 2. Stage-game structure

1. **War strictly dominates Peace** at every opponent-war count (6>4, 3>1, 0>−1).
   One-shot NE = all-War (0,0,0); all-Peace (4,4,4) Pareto-dominates. All value of Peace is
   *conditional-future* — an investment in opponents' continued Peace.
2. **The retaliation externality is internalized by the matrix**: once either opponent wars,
   my Peace pays ≤1 while War pays 3 (or 0). Best response to *any* observed war is War.
   ⇒ One habitual aggressor unravels a triple in one round if others best-respond ⇒ the game is
   **far more cascade-prone than 2-player IPD**. Standard NIPD "targeted punishment" is impossible
   here (my War hits both opponents) — the correct 3P-specific adaptation is fast *pair
   classification*: hopeless pairs get War, unburned coalitions hold Peace at 4/round.
3. **Ceiling table** (per round): exploit 2 saints = 6; farm Tolerant-class at safe rate ≈ 4+2r\*
   (r\*≈0.4 ⇒ ≈4.8); mutual peace = 4; War vs 1 warrior = 3; all-War = 0; Peace vs any war ≤ 1.
4. **[Discovered in C1c-v2a verification] TFT-pair self-lock**: a pair of `war-iff-either-opponent-warred`
   (TFT_any) classmates locks *itself* into all-War from the first shared war onward — each copies the
   other, so no third-party action (contrition, peace, anything) breaks the B↔C lock; best response
   ≈ War (≈0.16/rd over a full match incl. early rounds). Verified by direct trace (contrite
   reference policy: −0.73/rd; commit: 0.09/rd). Consequences: (a) TFT_any-pair triples are a
   ~0/rd wash for every entrant — no relative edge exists there; (b) our edge vs the TFT-family
   comes from *mixed* triples ({TFT_any, X}); (c) since TFT_any is the modal classmate archetype,
   many tournament matches are predetermined washes — the ranking is decided on the exploitable
   and responsive masses.
4. **Random-opponent EV** (War 3.0 > Peace 1.0 against a lone 50/50; joint-pair math in
   brainstorm) ⇒ classification must be **joint over the pair**, never per-opponent naive.

## 3. Cascade & feud economics (brainstorm priors — campaign certifies)

- Probe EV = +2 immediate − 4·E[trap-rounds] vs responsive pairs (opportunity cost 4/rd).
  Positive always vs saints (no trap); endgame-only vs responsive pairs once
  `2q ≥ c(1−q)`, q = 1/(111−t) hazard of match end.
- Trap costs per one-shot probe: GTFT pair ≈ 9 pts; T4T pair ≈ 6–10 (2-War state is
  absorbing while I War — must exit with Peace); Tolerant pair ≈ 3–5 (cumulative flip);
  saints 0.
- 3-cycle bleed states (deterministic rotation): 2.67/rd (1-War phase) and 1.67/rd (2-War)
  vs 4 static ⇒ stochastic leniency ε≈0.2 breaks phase-lock at ≤1–2% cost.
- **The 100-way race is a tail race**: P(rank 1) ≈ P(my total > max of 99 others). One
  0/rd-locked match ≈ −4·100 relative to a normal match ⇒ variance control (never lock at 0,
  never be the permanent sucker) is first-order, not a nicety.

## 4. Pool model

Plausible prior + stress compositions (nice-heavy, retaliator-heavy, exploiter-heavy,
adversarial-elite-heavy, uniform, strong-classmate-heavy, LLM-pattern-heavy; both pool-size
interpretations). Compositions are the held-out dimension; tuning never sees final validation
compositions. See `LITERATURE.md` §6 for archetype frequencies (planning ranges).

## 5. Candidate arms & verdicts

- **A — GTFT-3P baseline** (control): Peace; War iff either opp warred last round; 1-shot
  forgiveness. Solid vs responsive majority; misses saint exploitation; disguised-saint trap
  ≈ −240/match.
- **B — k-lenient probabilistic retaliator**: War iff any opp ≥2 wars in last 3; dithered
  amnesty. Cycle-immune; no upside.
- **C — pure classifier+exploiter**: fastest upside; highest misread cost.
- **D — WSLS-3P: REJECTED** — stay-threshold ≥1 never harvests saints (4 → stays Peace);
  the harvesting variant (shift on <4) self-detonates vs GTFT pairs (locks 0/rd). One-round
  memory cannot resolve a 5-class space.
- **E — Hybrid (ship candidate)** = B-backbone + C-two-test-classifier + anti-lock + endgame:
  1. Nice opening; two-test classifier (one-shot probe ~rd 3–5; 3-round 0.6-burst ~rd 10–12
     discriminating Tolerant cumulative flip vs saint silence; 0.1 trickle inside test window
     only; non-periodic, own-PRNG probe selection);
  2. class-conditional policy: confirmed saints → commit War (6/rd); Tolerant-class → farm at
     r\* ≈ 0.4 (first-crossing flip risk ≤1–2%; adjust ±0.1–0.2 for partner war-rate);
     Nasty/Random/dead → War; responsive unburned → Peace;
  3. anti-lock: never Peace on 2-War round; sustained squeeze → commit War (0 > −1);
     late opponent war-commit → immediate War (3/rd not 0/rd);
  4. dithering (narrow): ε≈0.2 leniency, ±1-rd amnesty jitter — breaks cycles & cycle-fitters;
  5. endgame ramp: War-probability rising with hazard q = 1/(111−t), class-specific trap-cost
     thresholds (responsive: last ~1–3 rds; saint/tolerant-class: from ~rd 90).
- Brainstorm pool-EV priors: E ≈ 424/match vs GTFT 386 vs trickle-only 374
  (20% S / 40% G+U / 20% T / 10% N / 10% R pool). **Certify or kill in Phase C.**

## 6. Methodology (binding)

Common random numbers for every A/B; ≥10⁵ tournaments overnight on 16 cores; stratified pool
draws; hold-out compositions; adversarial self-play loop (GA/hill-climb generating opponents
against our current best until none can displace it); report P(#1) with CIs, mean rank,
P(top-3), worst-case composition rank. Objective: maximize P(rank #1).

## 7. Residual risks (real-grader unknowns)

Signature const-ness (harness non-const = ground truth; port adapts to either), self-triples
(robust to both; handshake rejected — ≈2% incidence, net-negative EV), exact pool size "~100",
rerun count (robustness across compositions covers it), matrix fidelity (sample matrix assumed
authoritative; flagged).
