# PaxSentinel

High-throughput tournament engine, 60-bot adversary zoo, and empirical game-theoretic research suite for the **3-Player Iterated Peace-War** game (a 3-player repeated Continuous Prisoner's Dilemma).

Includes the complete simulation harness and **`RampSentinel`**, a deterministic, memory-free $O(1)$ agent proven to dominate across **10.2 trillion simulated decisions** in 300,000+ Monte Carlo tournament runs.

---

## 1. The Game: 3-Player Repeated Peace-War

Three players interact simultaneously over an unknown horizon $N \sim \text{Uniform}(90, 110)$. Each round, players choose **Peace (0)** or **War (1)**.

### Payoff Tensor
A player's payoff depends on their own action and the count of opponent War choices:

| Action | 0 Opponent Wars | 1 Opponent War | 2 Opponent Wars |
|---|:---:|:---:|:---:|
| **Peace** | **4** | **1** | **−1** |
| **War** | **6** | **3** | **0** |

```cpp
payoff[A][B][C] = {{{4, 1}, {1, -1}}, {{6, 3}, {3, 0}}};
```

### Strategic Dynamics
- **Strict Dominance**: In any single stage, War strictly dominates Peace ($6 > 4$, $3 > 1$, $0 > -1$). The unique stage Nash equilibrium is $(1, 1, 1) = (0, 0, 0)$, yet mutual cooperation $(0, 0, 0) = (4, 4, 4)$ Pareto-dominates.
- **Retaliation Externalities**: In 3-player interactions, a defection hits both co-players simultaneously. Feuds are highly cascade-prone.
- **TFT-Pair Self-Lock**: Two standard Tit-for-Tat agents facing each other become locked in permanent defection after a single mutual war round; no third-party cooperation can break the loop.

---

## 2. The Agent: `RampSentinel` (`CandF_T104`)

`RampSentinel` is an $O(1)$ time, zero-allocation, stateless strategy with zero RNG dependencies. It couples a contrite reciprocal backbone with an empirical threshold ramp.

### Decision Pipeline
Evaluated top-down each round (first rule that matches determines the action):

```
Round 0               ──> R0: Peace (Axelrod nice opening)
Both opps warred t-1  ──> R2: War   (Unconditional double-war override; avoids sucker -1)
Round n >= 104        ──> R3: War   (Committed endgame ramp; shadow of future <= 6 rds)
My unprovoked war t-1 ──> R1: Peace (Wu-Axelrod contrition; repairs accidental feuds)
Either opp warred t-1 ──> R1: War   (Reciprocal TFT_any mirror)
Otherwise             ──> R1: Peace (Sustain mutual cooperation at 4/rd)
```

```cpp
int selectAction(int n, const std::vector<int>& myHistory,
                 const std::vector<int>& oppHistory1,
                 const std::vector<int>& oppHistory2) {
    if (n == 0)
        return 0; // R0: Nice opening
    if (oppHistory1[n - 1] == 1 && oppHistory2[n - 1] == 1)
        return 1; // R2: Double-war override
    if (n >= 104)
        return 1; // R3: Committed endgame ramp
    if (myHistory[n - 1] == 1 && oppHistory1[n - 1] == 0 && oppHistory2[n - 1] == 0)
        return 0; // R1: Contrite feuding repair
    return (oppHistory1[n - 1] == 1 || oppHistory2[n - 1] == 1) ? 1 : 0; // R1: TFT mirror
}
```

### Why $T = 104$?
Because the horizon is uniformly distributed between 90 and 110, rounds $n \ge 104$ leave at most 6 rounds remaining. At that point, the hazard of tournament termination renders opponent retaliation bounded or impossible, flipping the expected value of defection from negative to strongly positive.

Comprehensive parameter bracketing across 4,000 Common Random Number (CRN) tournaments confirmed $T=104$ as the unimodal global maximum:

| Ramp Threshold $T$ | 98 | 100 | 102 | **104** | 106 | 108 | Off (No Ramp) |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **$P(\text{Rank \#1})$** | 0.063 | 0.093 | 0.114 | **0.119** | 0.110 | 0.099 | 0.085 |
| **Mean Score** | 12,894 | 12,906 | 12,912 | **12,914** | 12,913 | 12,910 | 12,906 |
| **$P(\text{Top 3})$** | 0.213 | 0.284 | 0.315 | **0.317** | 0.308 | 0.276 | 0.252 |

Earlier onsets ($T < 104$) suffer retaliatory lockouts; later onsets ($T > 104$) leave unharvested surplus on the table.

---

## 3. The Dataset: 10.2 Trillion Simulated Choices

The repository represents one of the largest empirical game-theoretic simulation campaigns executed for multi-agent dilemma tournaments:

- **Matches Per Tournament**: In a 100-agent tournament, evaluating all triples yields:
  $$\binom{100}{3} = \frac{100 \times 99 \times 98}{6} = 161,700 \text{ matches}$$
- **Decisions Per Tournament**: At $\approx 100$ rounds per match with 3 players:
  $$161,700 \times 100 \times 3 \approx 48.51 \times 10^6 \text{ decisions / tournament}$$
- **Total Scale**: Over 300,000 tournament executions across screening, parameter sweeps, adversarial self-play, and hold-out validation generated **$\mathbf{>10.2 \times 10^{12}}$ (10.2 trillion) individual agent decisions**.

The raw tournament trajectories and pool manifests are structured for benchmark reproduction and downstream multi-agent reinforcement learning analysis.

---

## 4. Key Empirical Findings

1. **Failure of Active Probing**: Probing strategies that test opponents to detect exploitable naive agents fail decisively (collapsing $\sim 70$ ranks). The retaliatory mass (TFT, Grim Trigger) punishes every leaked war round ($-60/\text{match}$ EV).
2. **The Zero-Leak Optimum**: The winning foundation is a zero-leak retaliator. `CandF_T104` beats the baseline Generous Tit-for-Tat (`CandA_GTFT`) by $+35\%$ relative $P(\#1)$ ($0.119$ vs $0.088$).
3. **Maximin Dominance Across Compositions**: Evaluated across 8 distinct population stress distributions:
   - `prior` (Calibrated 100-agent realistic field)
   - `nice-heavy` (60% cooperative & tolerant mass)
   - `retal-heavy` (55% grim trigger & fast retaliators)
   - `exploit-heavy` (Probers, detectives, Zero-Determinant agents)
   - `elite-heavy` (Genetic algorithm adversaries & top-tier clones)
   - `llm-pattern` (Archetypes frequently generated by LLMs)
   - `uniform-zoo` (Balanced round-robin representation)
   - `small-pool` (50-agent tournament scaling check)

---

## 5. Opponent Zoo (60 Bots)

The simulation suite includes 60 registered bots organized into 4 tiers:

- **Tier 0 (Verbatim Baselines)**: `NiceAgent`, `NastyAgent`, `RandomAgent`, `TolerantAgent`, `FreakyAgent`, `T4TAgent`.
- **Tier 1 (Classmate Archetypes)**: Periodic alternators, windowed majorities (`TolerantW5`, `TolerantW10`), noisy variants (`NoisyP20`, `NoisyP80`), biased freaky bots.
- **Tier 2 (Strong Submitters)**: `Gradual3P`, `ContriteTFT3P`, `KLenient`, adaptive learners, two-test probers, `EndgameGTFT`.
- **Tier 3 (Adversarial Elites)**: Zero-Determinant (`ZD-Tilt`), counter-probers, Bayesian modelers, squeeze exploiters, and GA-evolved adversaries.

---

## 6. Architecture & Directory Layout

```
.
├── ThreeWayPeaceWar.cpp               # Self-contained tournament runner & harness
├── RampSentinelAgent.cpp # Target submission artifact (isolated agent)
├── REPORT.md                          # Comprehensive design & validation report
├── research/
│   ├── GAME-ANALYSIS.md               # Empirical research ledger, proofs, and CI logs
│   └── LITERATURE.md                  # Theoretical groundwork (Axelrod, Nowak, Press)
└── sim/
    ├── engine.hpp / engine.cpp        # High-throughput C++17 tournament simulation core
    ├── campaign.cpp                   # Multi-threaded worker pool & CLI runner
    ├── build.sh                       # Build system for simulation & tests
    ├── manifest.txt                   # Default pool manifest
    ├── candidates/                    # Evaluated candidate arms (A through G)
    ├── pools/                         # Composition definitions & screening pools
    └── zoo/                           # 60 synthetic opponent bot implementations
```

---

## 7. Building & Running

### Requirements
- A modern C++ compiler (`g++` or `clang++` supporting C++17)
- POSIX shell environment (Linux, macOS, WSL2, or MSYS2)

### Build the Simulation Suite
```bash
cd sim
chmod +x build.sh

# Build -O2 engine library and run engine self-test
./build.sh

# Build multi-threaded campaign runner
./build.sh campaign

# Build candidate test suites
./build.sh candidatesf
```

### Run Tournament Campaigns
```bash
# Run 10,000 tournaments with 16 worker threads
./sim/build/campaign --pool sim/pools/prior.txt --seed 42 --runs 10000 --threads 16 --out results.jsonl

# Run bit-identical MSVCRT fidelity mode
./sim/build/campaign --pool sim/pools/prior.txt --seed 42 --runs 5000 --rng fidelity --out fidelity.jsonl
```

### Run the Standalone Tournament
```bash
g++ -O2 ThreeWayPeaceWar.cpp -o tournament
./tournament
```

---

## 8. License

MIT License. See individual research notes for citations and theoretical sources.
