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
