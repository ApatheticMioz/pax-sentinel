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

### 5.0 Phase-C screening verdict (20,000 CRN runs, prior pool, 2026-10-07) — E/C killed, pivot to F

Screening: arms {A, B, C, E} × 5,000 runs each, prior composition, seed 731842, mode
distinct. Fair single-entry comparison (coworker verification, ties→index-0):

| Entry (as our agent) | mean score | mean rank | P(#1) |
|---|---|---|---|
| CTFT3P | 12,906 | ~1.1 per-copy | **0.088** |
| TFT_any | 12,906 | ~1.1 per-copy | **0.083** |
| CandA_GTFT | 12,825 | 13.3 | 0.0008 |
| CandB_KLenient | 12,200 | 54.9 | 0.000 |
| CandE_Hybrid | 10,290 | 69.5 | 0.000 |
| CandC_Classifier | 10,202 | 72.1 | 0.000 |

Findings (CRN-paired, sd ≈ 65 on 5,000-run diffs):
1. **Probe-based exploitation is killed on the prior composition.** E/C collapse ~70 ranks;
   A > E by +2,535 (P(beat) = 1.000, 5000/5000). Root cause: the retaliatory mass
   (TFT-family 28%, grim 8%, tolerant 19%) punishes every leaked war-round; probe EV ≈
   −60/match expected (a provoked grim locks at 0/rd ≈ −400/match at ~15% grim-per-pair
   incidence; saints — the only prey worth ≈ +190/match — are indistinguishable from clean
   TFTs without probing, and probing is exactly what poisons the TFT mass).
2. **The crown archetype is the zero-leak fast retaliator.** CTFT3P/TFT_any per-entry
   P(#1) ≈ 0.083–0.088 at mean 12,906; A is −82 short (CRN-paired; −141 vs the best
   correlated copy). The 100-way race is a threshold race: beat ≈ 12,906 and the crown
   moves. (TFT_any's 10 correlated copies take the top slot in 91% of runs ties-included;
   per-entry crown chance is the 0.083 figure.)
3. **The unexploited lever is the endgame.** No top-cluster archetype ever defects first,
   even at round 109 of ≤110. Against the clean ~4/rd mass, war only when the horizon hazard
   q = 1/(111−t) makes retaliation impossible-or-bounded is worth +1..+4/match across
   thousands of matches — a class-free exploiter that needs no classifier and cannot lock
   (the match ends). Zoo EndgameGTFT (rank 42, mean 12,520) warns the onset must be late.

The arms list below predates screening; C and E are superseded by this verdict, and the
ship candidate is now **F** (§5.x, implemented as CandF_Ramp).

### 5.x Arm F — RampEndgame (ship candidate v2)

CTFT3P backbone + double-war override + endgame ramp: (R0) nice opening; (R1) contrite-TFT
echo; (R2) never Peace when both opps warred last round (A's forgive-1 peaced into 2-war
states at −1/rd — primary suspect for the −82 mean-gap vs TFT_any); (R3) endgame ramp for
n ≥ 90 (horizon uniform 90–110 is never known to the agent), two parameterizations:
threshold T (War iff n ≥ T) and q-hazard (War w.p. min(1, c/(111−n))); ramp-wars committed
(no contrition inside the window); no probes ever; own PRNG. Grid arbitrates ramp shape and
onset.

### 5.y Ramp grid on prior (4,000 CRN runs/arm, seed 731842) — first results

| Variant | P(#1) | 95% CI | mean score | CRN-paired vs T104 |
|---|---|---|---|---|
| **CandF_T104** | **0.119** | [0.109, 0.129] | 12,914 | — |
| CandF_T106 | 0.110 | [0.100, 0.120] | 12,913 | −0.9 [−2.1, +0.3] (tie) |
| CandF_T108 | 0.099 | [0.090, 0.109] | 12,910 | −4.1 |
| CandF_mode0 (no ramp) | 0.085 | [0.077, 0.094] | 12,906 | −7.3 |
| **CandF_mode2 (q-hazard c=3)** | **0.003** | [0.002, 0.006] | 12,831 | **−82.2** |

Findings:
1. **R2 override closes the archetype gap**: mode0 sits exactly on the CTFT3P/TFT_any
   crown archetype (12,906; P(#1) 0.085 ∈ archetype's [0.083, 0.088]) — up from A's 0.0008.
2. **The threshold ramp is a real, clean edge**: T104 > archetype by +35% relative P(#1)
   (0.119 vs 0.088), CRN-paired diffs resolve ±2 points on 12,900 means.
3. **Ramp SHAPE dominates ramp onset**: the q-hazard curve (probabilistic wars from n=90,
   c=3.0) loses −82 to T104 — it pays full retaliation-lock costs while long horizons
   remain, forfeiting the threshold ramp's one protection (commit only when ≤6 rounds
   remain, so retaliation cannot land). Hard-late ≫ soft-early.
4. Returns diminish toward later onsets (T108→T106 +3.2, T106→T104 +0.9); bracket
   extension T98/T100/T102 probes the lower floor (zoo EndgameGTFT at rank 42 warns much
   earlier onsets collapse).

### 5.z Ramp onset bracket (complete) — T104 frozen

Full onset curve (4,000 CRN runs/arm, seed 731842):

| T | 98 | 100 | 102 | **104** | 106 | 108 | off |
|---|---|---|---|---|---|---|---|
| P(#1) | .063 | .093 | .114 | **.119** | .110 | .099 | .085 |
| mean | 12,894 | 12,906 | 12,912 | **12,914** | 12,913 | 12,910 | 12,906 |
| P(top-3) | .213 | .284 | .315 | **.317** | .308 | .276 | .252 |

Smooth unimodal curve, peak bracketed on both sides. CRN-paired vs T104: T102 −1.6
[−2.9, −0.3] (CI excludes 0), T100 −7.4, T98 −19.2, T106 −0.9 [−2.1, +0.3], T108 −4.1.
**Ramp frozen: ramp_mode=1, ramp_T=104. Ship candidate: CandF_T104.** The mechanism reads
cleanly: earlier onsets pay retaliation locks while long horizons remain (T98 loses
−19 to T104); later onsets forfeit harvest rounds (T108 −4). T104 = "commit only when
≤6 rounds remain" is the empirical crossover on this composition; hold-out families test
whether the crossover moves with composition.

### 5.za Hold-out validation (63,000 runs: 7 families × {T104, T106, CandA} × 3,000, fresh seeds) — echo-tail discovery

P(#1) by family (T104 / T106 / CandA), CRN-paired mean vs T104 in parens:

| Family | T104 | T106 | CandA | crown holder |
|---|---|---|---|---|
| exploit-heavy | **0.172** | 0.147 | 0.000 (−273) | (F-class wins) |
| elite-heavy | 0.171 | 0.189 (+3.8) | 0.000 (−400) | (F-class wins) |
| small-pool | 0.139 | 0.140 | 0.018 (−25) | (F-class wins) |
| uniform-zoo | 0.001 | 0.001 | 0.000 (−34) | (mixed) |
| nice-heavy | 0.005 | 0.003 | **0.027 (+28)** | TF2T 39%, TolerantW5/10 36%, Alternator 15% |
| llm-pattern | 0.000 | 0.000 | 0.000 (+79) | **TolerantW5 79%**, TolerantAgent 21% |
| retal-heavy | 0.000 | 0.000 | 0.000 (+21) | **Grim_maj 100%** |

**The echo-tail mechanism** (discovered here): the committed threshold ramp wars at n ≥ T
against *clean responsive* pairs — they echo our war, we echo back (R1/R2), and the tail
locks at 0/rd (~−22/match). Crown archetypes in cooperative-heavy pools (TolerantW5 79% of
llm-pattern) keep their tails pristine at 4/rd and win the mean race while we burn ours.
On `prior` the saint/tolerant harvest outweighed the echo cost; on TFT-heavy compositions
it does not. **The ramp's value is "harvest only what cannot retaliate" — the committed
threshold over-harvests.**

**Fix — ramp mode 3 (self-limiting probe cycle)**: war from T; if the pair echoes
(both opps warred after our ramp war) → one peace repair round (echo decays) → re-probe.
Vs silent pairs: full commit +2/rd (untouched). Vs responsive pairs: ≈ −0.5/rd instead of
−4/rd. Precedence inside the ramp window: R3-repair > R2 > R1.

### 5.zb Mode-3 certification: REJECTED; CandF_T104 frozen as ship candidate

Mode 3 (echo-repair probe cycle, T=104), 4,000 runs/arm on the three decisive families:

| Family | mode 3 | T104 | CandA |
|---|---|---|---|
| prior | 0.088 / 12,906 | **0.119 / 12,914** | 0.001 |
| llm-pattern | 14,415 | 14,422 | **14,501** |
| nice-heavy | 0.004 | 0.005 | **0.027** |

The repair forfeits harvest on the crown pool (−0.119 → 0.088) and recovers ~none of A's
llm-pattern gap — **the echo-tail hypothesis is falsified**: A's cooperative-heavy lead is
not primarily TFT-echo repairable; the committed ramp is genuinely −EV when the crown
archetype is a pure coaster (TolerantW5 = 79% of llm-pattern crowns) and prey mass is
thin. Ship decision: **CandF_T104** — primary (prior) won by an order of magnitude, and
max-min across families: F's worst deficit to A is +79 (llm-pattern); A's worst deficit
to F is −400 (elite-heavy). Mode 3 retained in the registry as a falsified arm.

**Open adversary before freeze — "coast + harvest" (CandG_TolRamp)**: TolerantW5 backbone
+ mode-1 T104 ramp. If it beats F_T104 on both llm-pattern and prior, the backbone choice
reopens; if it wins only llm-pattern, the prior-primary trade stands.

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
