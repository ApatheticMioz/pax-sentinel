# Slice B1a — Tournament engine library + self-test

Single concern: **engine semantics + performance**. No CLI runner, no
fixture-comparison harness, no zoo bots (later slices).

## Files
| file                | role                                              |
|---------------------|---------------------------------------------------|
| `engine.hpp`        | public API: `AgentBase`, `Rng`/`MsvcrtRng`, `run_tournament`, `engine_self_test` |
| `engine.cpp`        | implementation + self-test body                   |
| `self_test_main.cpp`| tiny `main` guarded by `#ifdef ENGINE_TEST`       |
| `build.sh`          | g++ build, no cmake: `./build.sh [all\|opt\|run\|debug]` |

## Build / verify
```
./build.sh          # -O2 lib (-O2 -march=native -std=c++17 -Wall -Wextra) + self-test
./build.sh debug    # + -O0 -g -fsanitize=address,undefined self-test
```
Self-test prints PASS/FAIL per group, exact mismatch coordinates on failure,
and the measured fast-mode throughput. Exits 0 on all-pass.

---

# Slice B1c — Campaign CLI runner + threaded worker pool

Single concern: **batch tournament execution + checkpointing + JSONL output**.
No zoo bots, no analysis (later slices). Consumes the engine API as-is.

## Files
| file                          | role                                              |
|-------------------------------|---------------------------------------------------|
| `campaign.cpp`                | global `Registry` + `REGISTER_AGENT(name)` macro, builtin `AlwaysPeace`/`AlwaysWar`, CLI (`campaign_run`), worker pool, checkpoint/resume, JSONL writer |
| `campaign_self_test_main.cpp`| tiny `main` guarded by `#ifdef CAMPAIGN_TEST`     |
| `build.sh`                    | + `./build.sh campaign` target (runner + self-test) |

## Build / verify
```
./build.sh campaign   # -O2 runner (build/campaign) + -O2 self-test (build/campaign_test)
```

## CLI
```
campaign --pool manifest.txt --seed S [--mode distinct|self] [--runs K]
         [--threads T] [--out results.jsonl] [--rng fast|fidelity] [--resume]
```
- `--pool manifest.txt`: lines are `name [count]` (count default 1, `#`
  comments, blank lines ignored). The manifest is expanded to an ordered
  agent list (repeating `name` `count` times). Total agents = sum of counts,
  **capped at 200** (rejected otherwise).
- `--seed S`: run `i` uses seed `S + i` (deterministic per run).
- `--mode distinct` (i<j<k) or `self` (i<=j<=k).
- `--runs K`: number of runs (default 1).
- `--threads T`: worker threads (default 16).
- `--out results.jsonl`: output file (default `results.jsonl`).
- `--rng fast` (sfc64, default) or `fidelity` (msvcrt LCG).
- `--resume`: read the existing `--out`, skip run indices already present.

Output is one JSONL line per run, in run-index order:
```json
{"run":0,"seed":S,"mode":"distinct","pool":["AlwaysPeace",...],"scores":[...]}
```
`scores` is one value per agent (pool order) at full double precision
(`%.17g`, round-trips exactly).

## Common Random Numbers (CRN) — paired A/B protocol
For a controlled A/B comparison of two candidate bots:
1. Build two **identical** pool manifests, differing in exactly one agent
   (the candidate vs. the control), same position, same count.
2. Run **both** with the **identical `--seed`** and the same `--mode`,
   `--runs`, and `--threads`.
Because run `i` is driven only by seed `S+i` and the agent list, every run
index is a *common random number* pair: the same tournament draws feed both
candidates, so the only thing that varies between the two outputs is the
swapped agent. This makes paired score deltas far lower-variance than
independent runs (the standard CRN variance-reduction argument).

## Design decisions worth knowing
1. **Workers buffer, main thread flushes in run-index order.** Each worker
   writes its run's line into a pre-allocated `results[run]` slot (race-free:
   each run index is claimed by exactly one worker via an atomic counter).
   After all workers join, the **main thread** writes the file in run-index
   order under no contention. This is what makes two separate invocations
   **byte-identical** regardless of thread interleaving (a naive "append
   under a mutex" would interleave lines in completion order and break
   determinism). Progress lines (every 1000 runs) go to **stderr**.
2. **`--resume` rewrites the whole file in order.** On resume the existing
   `--out` is read back, already-completed lines are kept verbatim in their
   slots, only missing runs are computed, and the complete file is
   re-written in run-index order (truncated first). The result is
   **byte-identical to an uninterrupted run for ANY completion pattern** —
   not just contiguous early stops — because per-run seeds are deterministic.
3. **Builtins are registered unconditionally.** The spec asked for the two
   builtins under `#ifdef CAMPAIGN_TEST`, but the standalone `campaign`
   runner has no other bots yet in this slice, so it needs `AlwaysPeace`/
   `AlwaysWar` available to run any manifest. They are therefore registered
   unconditionally in `campaign.cpp`; the `CAMPAIGN_TEST` ifdef gates only
   the self-test harness (`campaign_self_test`). Later slices add
   zoo/candidate bots in their own TUs via `REGISTER_AGENT(name)` and link.
4. **RNG is per-tournament and thread-local.** `run_tournament` reseeds the
   engine's thread-local RNG from the run's seed, so a run's score is a
   pure function of (agent list, seed, mode, rng-kind) — the basis for both
   determinism and exact resume.

## Measured throughput (this run, 8-core host, `-march=native`)
100-agent distinct-mode tournament (161,700 matches each), fast RNG:
- **1 thread: ~12.9 runs/sec/core** (≈ README B1a baseline ~13.5).
- **8 threads (1:1 with cores): ~70 runs/sec** (≈ 5.4× single-core).
- **16 threads: ~71 runs/sec** — no gain over 8 on an 8-core host
  (oversubscription). Scale `--threads` to physical cores, not 2×logical.

## Verify (performed)
- (a) 100 runs, 6 bots (4×Peace 1×War 1×Peace), 16 threads → exactly 100
  JSONL lines, all parse, all scores finite, `seed == S+run`.
- (b) two separate invocations, same `--seed` → **byte-identical**
  (SHA-256 match, repeated 3×).
- (c) `--runs 30` then `--resume --runs 100` → final file **byte-identical**
  to an uninterrupted 100-run invocation.
- (d) 16 threads: ~71 runs/sec (100-agent distinct, 2000 runs).

## Preserved sample semantics (ThreeWayPeaceWar.cpp)
- `payoff[A][B][C] = {{{4,1},{1,-1}},{{6,3},{3,0}}}`; player X reads
  `payoff[X][next][prev]` (B is X's next, C is X's prev).
- Per-round selectAction call order A, B, C; each agent sees the histories
  as they were before this round (length `n`).
- Histories extended only AFTER all three payoffs collected.
- Per match: construct A, B, C **first**, then draw the horizon
  `90 + round(20 * (float)rand() / RAND_MAX)` (so constructor-side RNG
  draws, e.g. the sample's FreakyAgent, happen before the horizon draw).
- Scoring uses the sample's exact types: `int matchResults[3]` accumulating
  per match, then `float totalScore[i] += matchResults[x] / rounds` (float
  division). Public return is `std::vector<double>` (float totals, cast).
- Per-match fresh construct + delete of all three agents.
- Both enumeration modes: `kSelfInclusive` (i<=j<=k, the sample's loop) and
  `kDistinct` (i<j<k).

## Two RNG backends
- **kMsvcrt (fidelity):** MSVCRT-compatible 32-bit LCG
  (`seed*214013 + 2531011`, out `(seed>>16)&0x7FFF`, `RAND_MAX=0x7FFF`),
  exposed as `fidelity_rng()`. Sample-faithful types + verbatim horizon
  formula ⇒ a later fixture comparison is bit-exact with an MSVCRT build
  of the original program (same seed + agent sequence).
- **kSfc64 (fast):** splitmix64-seeded sfc64, exposed as `fast_rng()`;
  flat preallocated history storage (reserved once per tournament, index
  appends, no per-round realloc); identical payoff/horizon/scoring.

## Gotchas worth knowing for later slices
1. **sfc64 needs its counter.** The first sfc64 draft was the *constant*
   variant (it always returned the same word, so every horizon landed on
   one value). The correct form increments a counter per call:
   `result_ += mask_` each call.
2. **Do not use glibc `rand()` for fidelity bots.** The original sample's
   stochastic bots (`RandomAgent`, `FreakyAgent`, `T4TAgent`) call `rand()`
   directly. On a non-MSVC host `rand()` is a different algorithm with
   `RAND_MAX = 2^31-1`, so `rand()/RAND_MAX` will NOT reproduce the
   MSVCRT stream. Port them to draw from `fidelity_rng().rand()`
   (kMsvcrt) or `fast_rng().rand()` (kSfc64) — one canonical call-site per
   mode keeps the stream bit-exact.
3. **LCG fixed-point seeds.** A few small integer seeds (e.g. 1, 77, 999)
   make the msvcrt LCG degenerate: `seed = seed*214013 + 2531011` lands
   back near the seed, so the first draws are not well-mixed and the
   horizon can stick to 90. Self-test uses well-mixed seeds (2024, 31337)
   for the match-count/score checks; the horizon-distribution check uses a
   large seed (12345) that covers all values 90..110 in 10k draws.

## Measured throughput (this run)
Fast mode, 100 always-peace agents, per core (8-core host, `-march=native`):
- distinct (C(100,3) = 161,700 matches each):  **~13.5 tournaments/s**
- self-inclusive (C(102,3) = 171,700 matches each):  **~12.8 tournaments/s**

Both exceed the target of ≥5 full DISTINCT-mode 100-agent tournaments per
core-second.

---

# Slice B2 — Opponent zoo, Tiers 0 + 1

Single concern: **zoo bots + their behavioral smoke tests**. No candidates,
no analysis (later slices). Bots are registered in the B1c `Registry` via
`REGISTER_AGENT(name)`; the `Registry` class and the macro now live in
`zoo/zoo_common.hpp` (single source of truth — `campaign.cpp` includes it).

## Files
| file                    | role |
|-------------------------|------|
| `zoo/zoo_common.hpp`   | `Registry` + `REGISTER_AGENT` (shared with campaign.cpp), `kZooPayoff`, engine-RNG draw helpers (`zoo_draw_bit`/`zoo_draw_uniform` on `fast_rng()`) |
| `zoo/zoo_t01.cpp`     | Tier 0 (6 sample bots, RNG routed through the engine) + 28 Tier-1 variants; one factory + one `REGISTER_AGENT` per name |
| `zoo/zoo_t01_test.cpp`| scripted-history unit tests (≥18 representative bots) + all-zoo 5-run smoke tournament |
| `build.sh`            | + `./build.sh zoo` target |

## Build / verify
```
./build.sh zoo    # -O2 -Wall -Wextra; 33 checks, exit 0 on all-pass
```

## Registered names (33)
- **Tier 0:** NiceAgent, NastyAgent, RandomAgent, TolerantAgent, FreakyAgent,
  T4TAgent.
- **Tier 1:** TFT_any, TFT_maj, TF2T_any, TF2T_maj, Grim_any, Grim_maj,
  GTFT_any_g10, GTFT_any_g30, WSLS_naive, Alternator, PeriodicPPW,
  Freaky25, Freaky75, BrokenTFT_1, Joss10, Detective, NoisyP20, NoisyP80,
  TolerantW5, TolerantW10, PavlovMaj, RandomWalk, Grim2Any,
  GrimAnyForgive1, MajWindow3, TitFor2TatAvg, SuckerFirst.

## RNG contract
Every stochastic bot draws **only** from the engine's thread-local `fast_rng()`
(sfc64), never `::rand()` and never time-seeded; the engine reseeds it from
the run's seed, so a run is a pure function of (pool, seed, mode, rng-kind).
`FreakyAgent`/`FreakyP` draw their one per-match constant in the constructor
(preserving the sample's "constructor draw precedes the horizon draw"
ordering); every other constructor is parameter-only.

## Rule-ambiguity decisions (literal readings — flagged per dispatch)
1. **NoisyP50 not registered.** The dispatch lists it as "NoisyP50 (=Random
   tier0 variant, skip if dup)". Its literal rule (iid war p=0.5) is
   behaviorally identical to the Tier-0 `RandomAgent`, so it is omitted to
   avoid a duplicate name in the pool. (28 Tier-1 bots implemented → 33 total
   registered, still ≥ the "30+ agent pool" requirement.)
2. **Grim_anyForgive1 / "10 consecutive rds"** uses the last-10 window
   (`war_count_last(h,10)==0` for both opponents), i.e. a 10-round calm tail.
3. **MajWindow3** = a majority (≥2) of the last ≤3 rounds each had *at least
   one* opponent at war (the "majority of (opp1,opp2) warred" phrasing has no
   strict 2-opponent majority within a single round, so the round-level
   reading is the operative one).
4. **TitFor2TatAvg** = average over all opponent-action slots in the last ≤4
   rounds (`2*w > total`), the literal "avg opp war-rate".
5. **RandomWalk trend** = sum-of-my-last-5-payoffs minus the 4/rd all-peace
   baseline (`4*cnt`), clamped; all-peace history → trend 0 → p=0.5.
6. **PavlovMaj** collapses to the same payoff-threshold switch as WSLS_naive:
   in this two-opponent tensor there is no distinct "majority opponent last
   move" to use as context, so the payoff rule is the operative part.

## Test verdicts (33 checks, all PASS)
- 18 scripted-history unit checks (deterministic bots) + 9 seeded-stochastic
  checks (predict the draw with a throwaway `Rng` of the same seed).
- Smoke tournament: 33-bot pool, 5 runs, fast mode, distinct enumeration —
  no exceptions, all scores finite, same-seed determinism holds.
- **Sanity caveat:** the requested inequality "NiceAgent avg vs saint-pair >
  NastyAgent avg vs saint-pair" is **FALSE** under the literal payoff tensor:
  in {Peace,Peace,Peace} a Nice player scores 4/rd, but a Nasty player in
  {War,Peace,Peace} scores `payoff[1][0][0]=6`/rd (both opponent slots are
  0). The test asserts the exact verifiable values (Nice=4, Nasty=6) and
  flags the direction rather than the worded inequality. The second sanity
  ("Grim_any survives vs Nasty-pair better than NiceAgent") **holds**:
  Grim_any ≈ 0/rd (war vs two war = `payoff[1][1][1]=0`) > Nice = −1/rd
  (`payoff[0][1][1]=-1`).
