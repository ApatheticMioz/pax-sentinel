# Assignment 3 — Three-Way Peace-War: Design & Validation Report

**Ship agent:** `RampSentinelAgent` in `ThreeWayPeaceWar.cpp` (frozen lab name
`CandF_T104`; rename point marked in-file).
**Objective:** maximize P(rank #1) among ~100 submitted agents in the repeated tournament.
**Method:** literature review → game-theoretic design → 60-bot opponent zoo → bit-faithful
simulation harness → ~300,000 common-random-number tournament runs → adversarial closure
→ fresh-seed freeze validation. Every shipped rule maps to a citation, a measured number,
or a ground-truth constraint — no exceptions.

---

## 1. Executive summary

The stage game (War strictly dominates Peace: 6>4, 3>1, 0>−1) makes cooperation valuable
only as a *conditional investment*. Screening 20,000 CRN-paired tournaments over a
calibrated 100-agent pool showed:

1. **Probe-based exploitation loses** (arms C/E collapsed ~70 ranks): the pool's
   retaliatory mass punishes every leaked war-round, and a provoked grim locks at 0/rd
   (~−400/match) at ~15% grim-per-pair incidence — probe EV ≈ −60/match.
2. **The crown archetype is the zero-leak fast retaliator** (TFT_any/CTFT3P: per-entry
   P(#1) ≈ 0.083–0.088 at mean 12,906). The 100-way race is a threshold race on mean score.
3. **The unexploited lever is the endgame**: no classical archetype ever defects first,
   even at round 109 of ≤110.

The ship agent is therefore: **contrite-TFT backbone (zero leak) + double-war override +
committed endgame war-ramp from round 104**. On the calibrated prior pool it takes the
crown in **11.9% of tournaments (95% CI [10.9%, 12.9%])** vs 8.3–8.8% for the best
classical strategy and 0.08% for the plain GTFT baseline — and it **max-min dominates**
every alternative across 8 stress compositions (worst deficit to any rival: +79 mean; the
rivals' worst deficits to it: −273 to −400).

## 2. Ground truth

All mechanics were verified against `ThreeWayPeaceWar.cpp` (the authoritative source):
payoff tensor `payoff[A][B][C]` (my action × #opp wars: Peace 4/1/−1, War 6/3/0); horizon
`rounds = 90 + round(20·rand()/RAND_MAX)` (uniform 90–110, **unknown to the agent**);
score `totalScore += matchResults[i]/rounds` per match; fresh agent construction per match
(no cross-match state, no opponent identity); non-const history references; shared
`srand(time(NULL))`. The simulation engine (`sim/engine.*`) reproduces this **bit-faithfully**
(fidelity gate 12/12 vs the original harness, MSVCRT rand rebound at link time).

## 3. The ship agent — rule chain and grounding

Evaluated top-down each round (first match wins); fully deterministic, zero RNG, no member
state, O(1)/round:

| Rule | Condition → action | Grounding |
|---|---|---|
| R0 | `n == 0` → Peace | nice opening (Axelrod 1980 property 1) |
| R2 | both opps warred last → War | never Peace into a 2-war round (0 > −1); closed the −82 mean-gap vs TFT_any (screening) |
| R3 | `n ≥ 104` → War | committed endgame ramp; harvest +2/rd when retaliation cannot land (unraveling: horizon hazard q=1/(111−t); Ichinose & Masuda 2018); T frozen by bracket (§5) |
| R1a | my last = War ∧ both opps peace → Peace | contrition (Wu & Axelrod 1995, CTFT) — repairs own unprovoked wars |
| R1b | else War iff either opp warred last | TFT_any mirror — the crown archetype's exact response policy |

Determinism is a deliberate property: the real harness's shared `rand()` (seeded once by
wall-clock) cannot perturb our behavior, and every match we play is a fixed function of
the opponents' action streams.

## 4. Campaign methodology (POOLS.md, binding throughout)

- **Engine**: C++ replica of the harness, ≥1.5 tournaments/sec at 16 threads (100-agent
  pools), two RNG paths (fast sfc64 for screening; MSVCRT-faithful for freeze), ordered
  deterministic output, checkpoint/resume.
- **Opponent zoo**: 54 archetypes in 4 tiers — Tier 0 (the 6 provided samples verbatim),
  Tier 1 (naive student variants: TFT family, broken-TFT, Joss, tolerant, grim, periodic,
  noisy), Tier 2 (strong submitters: probers, Gradual-3P, CTFT, Omega, lenient-k,
  endgame-optimizers, learners), Tier 3 (adversarial elites: ZD-3P, counter-probers,
  meta-bot).
- **Compositions**: 8 families (calibrated `prior` 99 opponents; nice-heavy, retal-heavy,
  exploit-heavy, elite-heavy, uniform-zoo, llm-pattern, small-pool 51).
- **Statistics**: common random numbers for every comparison (paired CRN seeds; observed
  paired-diff sd ≈ 40–65 on 12,900-point totals, resolving ±2-point edges); Wilson 95% CIs
  on P(#1); tuning pools and hold-out families separated; fresh seeds for freeze.

## 5. Results ledger

| Phase | Design tested | Verdict (primary = prior pool) |
|---|---|---|
| Screening (20k runs) | A: GTFT / B: k-lenient / C: classifier / E: probe-hybrid | **A best at 0.0008 P(#1); C, E killed** (−2,535 CRN-paired vs A) |
| Onset grid (28k runs) | committed ramp T ∈ {98…108} | **unimodal peak T=104: P(#1)=0.119**, bracketed both sides |
| Shape test | q-hazard probabilistic ramp | **rejected** (−82 vs T104: early probabilistic wars pay full locks) |
| Hold-out (63k runs, 7 families) | F_T104 vs A vs F_T106 | **F wins rival-rich pools by 270–400**; cooperative-heavy crowns belong to coasting archetypes (TolerantW5 = 79% of llm-pattern) |
| Mode-3 test (12k runs) | echo-repair probe-cycle ramp | **rejected** (crown pool 0.119 → 0.088; llm-pattern unrecovered — echo-tail hypothesis falsified) |
| Adversary (12k runs) | CandG "coast + harvest" (TolerantW5 + T104 ramp) | **rejected** (llm-pattern +563 but prior −325, P(#1)=0 — prior-primary rule settles) |
| **Adversarial loop** | all classes above + Tier-3 elites | **closed: no generated adversary displaces F_T104 on the primary** |
| Freeze | fresh seeds, fidelity RNG, 8 families × 2 arms + self-mode | *(running; table appended on completion)* |

## 6. Falsified hypotheses (kept as design ledger)

1. WSLS-3P (quantified kill pre-campaign: one-round memory cannot resolve the class space).
2. Active probing in any form on this composition (screening §5.0).
3. Earlier ramps T<104 and later ramps T>104 (bracket).
4. Stochastic hazard ramps (mode 2).
5. Echo-repair endgames (mode 3).
6. Coast+harvest backbone switch (CandG) — superior only where the crown archetype is
   already a coaster, at −325 on the primary.

## 7. Submission mechanics

1. Open `ThreeWayPeaceWar.cpp`; find `// RENAME: <student-id><Name>Agent before submission`.
2. Rename class `RampSentinelAgent` to `<student-id><Name>Agent` (valid identifier form of
   the convention in `instructions.txt`) — **3 occurrences** (comment, class declaration,
   `makeAgent` case 6).
3. Compile: `g++ -O2 -std=c++17 ThreeWayPeaceWar.cpp` — verified on g++ 13.3 (WSL) and
   14.2 (MSYS2 ucrt64). (On this dev machine only, `-static-libgcc -static-libstdc++` is
   additionally required due to a broken local MSYS2 install — missing
   `libgcc_s.dll.a`; repair: `pacman -S mingw-w64-ucrt-x86_64-gcc`. The submitted source
   needs no special flags on a healthy toolchain.)
4. Demo tournament runs as-is (our agent finishes top-2 of 7, order sane).

## 8. Risk register (real-grader unknowns)

| Risk | Exposure | Mitigation / evidence |
|---|---|---|
| Grader's base-class signature (const vs non-const refs) | compile error if the grader swaps their own harness base class | we submit the whole file, and the file's own base is non-const — consistent by construction; instructions' const mention contradicts the provided harness; harness treated as ground truth |
| Self-match-inclusive vs distinct triples | ranking mechanics differ slightly | freeze validates **both** enumeration modes; agent has no self-recognition logic to misfire |
| Pool size "~100" | mean-total scale changes | validated across 51–121 opponents (small-pool … elite-heavy) |
| Rerun count | single-run luck | deterministic agent + CRN-stable edge; ranking variance quantified across 4000-run samples |
| Payoff-matrix fidelity | if grader's tensor differs | matrix taken verbatim from the provided harness; engine fidelity-gated; flagged as unresolvable assumption |
| Opponent pool mis-specification | the real mix may differ from `prior` | max-min dominance across 8 compositions bounds the downside; crown-relevant archetypes (TFT/tolerant/grim/probers) all present in zoo |
| Classmates also optimized (user-mandated assumption) | exploit-heavy/elite-heavy families simulate this directly | F wins those families outright (0.172 / 0.171–0.189 P(#1)) |

## 9. Artifacts

- `ThreeWayPeaceWar.cpp` — the submission (rename point marked).
- `research/LITERATURE.md` — four research digests + citation ledger ([V]erified/[M]emory).
- `research/GAME-ANALYSIS.md` — payoff analysis, cascade economics, complete design ledger
  (§5.0–§5.zc) with every verdict's CRN numbers.
- `sim/` — engine, fidelity gate, zoo (54 archetypes), candidates A–G, campaign runner,
  8 pool manifests, analysis tools (`summarize.py`, `pool_ranks.py`), `POOLS.md` protocol.
- `sim/results/screening/` — ~190k tournament records (JSONL, CRN-seeded, deterministic).
- `sim/results/freeze/` — fresh-seed freeze validation (fidelity RNG, both modes).
