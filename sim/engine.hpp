// =============================================================================
// sim/engine.hpp — Three-Way Peace & War tournament engine (slice B1a)
//
// Faithful extraction of the tournament semantics from ThreeWayPeaceWar.cpp.
//
// Shared, sample-faithful semantics (both RNG modes):
//   * payoff[A][B][C] tensor verbatim; player X reads payoff[X][next][prev].
//   * Per round: A->selectAction, then B, then C; each agent observes the
//     histories as they were before this round (length n).
//   * Histories are extended only AFTER all three payoffs are collected.
//   * Each match constructs and deletes its own A/B/C agents (the horizon
//     draw comes after construction, so constructor-side rand() consumes
//     happen before the horizon draw — as in the sample).
//   * Per match: rounds = 90 + round(20 * (float)rand() / RAND_MAX).
//   * Scoring: totalScore[i] += matchResults[x] / rounds with the sample's
//     exact types: `int matchResults[3]`, `float totalScore[]`, so the
//     division is int/float promotion and the accumulation is float += float.
//     Public return type is std::vector<double> (float totals cast at the
//     boundary — widening is exact).
//
// Two execution backends (RngKind):
//   kMsvcrt ("fidelity"): MSVCRT-compatible 32-bit LCG (seed =
//     seed*214013 + 2531011, out = (seed>>16) & 0x7FFF, RAND_MAX = 0x7FFF)
//     exposed as fidelity_rng(); the sample-faithful types and horizon
//     formula are used verbatim so a later fixture comparison is bit-exact
//     with an MSVCRT build of the original program (given the same seed and
//     agent sequence).
//   kSfc64 ("fast"): own splitmix64-seeded sfc64 PRNG exposed as the Rng
//     handle fast_rng() for stochastic bots; flat preallocated history
//     storage with index appends (no per-round reallocation); agents see
//     std::vector<int>& views of length n; identical payoff/horizon/scoring.
//
// Enumerated triples: kSelfInclusive is i<=j<=k (the sample's exact loop);
// kDistinct is i<j<k.
//
// Thread-safety: RNG handles are thread_local; the engine is reentrant
// across threads, not within one tournament call.
// =============================================================================

#ifndef MAS_SIM_ENGINE_HPP
#define MAS_SIM_ENGINE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace mas_sim {

// Enumeration of (i, j, k) agent triples per match.
enum class TournamentMode {
  kSelfInclusive,  // i <= j <= k, exactly the sample's triple loop
  kDistinct        // i < j < k
};

// Which PRNG drives the per-match horizon draw (and bot randomness).
enum class RngKind { kMsvcrt, kSfc64 };

// MSVCRT's RAND_MAX — part of the bit-exact contract because the sample
// divides by it in the horizon formula.
constexpr int kMsvcrtRandMax = 0x7FFF;

static_assert(kMsvcrtRandMax == 0x7FFFu,
              "sample's horizon formula divides by RAND_MAX == 0x7FFF");

// 32-bit LCG matching the MSVC CRT rand(): each call does
//   seed = seed * 214013 + 2531011; return (seed >> 16) & 0x7FFF.
// reset(seed) plays the role of srand(seed).
class MsvcrtRng {
 public:
  explicit MsvcrtRng(int seed = 0) { reset(seed); }

  void reset(int seed) { state_ = static_cast<unsigned int>(seed); }

  int rand() {
    state_ = state_ * 214013u + 2531011u;
    return static_cast<int>((state_ >> 16) & 0x7FFFu);
  }

  unsigned int state() const { return state_; }

 private:
  unsigned int state_;
};

// 64-bit sfc64, seeded by the splitmix64 mixer. Deterministic in the seed.
// rand() returns a 15-bit value in [0, kMsvcrtRandMax] so the verbatim
// horizon formula stays in [90, 110] exactly as in the sample.
class Rng {
 public:
  explicit Rng(int seed = 0) { reseed(seed); }

  void reseed(int seed);

  uint64_t next() { return sfc64(); }                       // full 64 bits
  uint32_t next32() { return static_cast<uint32_t>(sfc64() >> 32); }
  int rand() { return static_cast<int>((sfc64() >> 48) & 0x7FFFu); }
  double uniform() { return (sfc64() >> 11) * 0x1p-53; }    // [0, 1)

 private:
  uint64_t sfc64();

  uint64_t s_;
  uint64_t mask_;
  uint64_t result_;
};

// Abstract agent, mirroring the sample's Agent (identical virtual
// signature; virtual destructor so `delete` through the base is safe).
// n is the number of completed rounds so far; the three vectors contain
// exactly the first n actions (own history, opponent 1, opponent 2).
//
// Stochastic agents: in kMsvcrt mode draw from fidelity_rng(); in kSfc64
// mode draw from fast_rng().
class AgentBase {
 public:
  virtual int selectAction(int n, std::vector<int>& myHistory,
                           std::vector<int>& oppHistory1,
                           std::vector<int>& oppHistory2) = 0;
  virtual ~AgentBase() = default;
};

// Per-thread RNG handles, re-seeded by run_tournament at the start of
// each tournament (one handle per RngKind; the other is left untouched).
MsvcrtRng& fidelity_rng();
Rng& fast_rng();

// Factory returning a fresh agent instance for one match slot.
using AgentFactory = std::function<std::unique_ptr<AgentBase>()>;

// Play a single match with three specific agents (already constructed).
// The horizon draw uses the given RNG kind/handle. `max_rounds` must be
// >= 110 (capacity for the flat storage). Returns the float match results
// (sample-faithful types) and the number of rounds played.
struct MatchResult {
  int rounds;
  float results[3];
};
MatchResult play_match(std::unique_ptr<AgentBase> a, std::unique_ptr<AgentBase> b,
                       std::unique_ptr<AgentBase> c, RngKind rng_kind,
                       int max_rounds = 110);

// Run a full tournament over `factories` (size must equal num_agents).
// Each match: construct agents from factories i, j, k, then draw the
// horizon, then play. Returns per-agent total score (sample-faithful
// float accumulation, cast to double at the boundary).
std::vector<double> run_tournament(const std::vector<AgentFactory>& factories,
                                   int num_agents, int seed,
                                   TournamentMode mode, RngKind rng_kind);

// Self-test: returns 0 on success, non-zero on failure. Prints [PASS] /
// [FAIL] per check, exact mismatch coordinates on failure, and the
// measured fast-mode throughput (tournaments/sec/core).
int engine_self_test();

}  // namespace mas_sim

#endif  // MAS_SIM_ENGINE_HPP
