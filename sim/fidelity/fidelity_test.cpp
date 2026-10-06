// =============================================================================
// sim/fidelity/fidelity_test.cpp — slice B1b: end-to-end fidelity gate
//
// For each seed in {1,2,3,42,12345,777777} and both enumeration modes
// (TournamentMode::kSelfInclusive and kDistinct), we:
//   (a) run the self-contained logic-verbatim fixture (sample_fixture.cpp),
//       which drives the MSVCRT LCG stream (via its redefined rand()/srand())
//       and produces six float totalScores;
//   (b) run the engine in fidelity mode (RngKind::kMsvcrt, whose
//       fidelity_rng() IS the same MSVCRT LCG, reset to the same seed) with the
//       six sample behaviors re-implemented as AgentBase subclasses that draw
//       from fidelity_rng() in exactly the call order the original induces
//       (constructors first, then horizon, then per-round);
//   (c) assert per-agent BITWISE equality (the fixture's float, widened
//       exactly to double, is byte-compared against the engine's returned
//       double).
//
// Prints a PASS line per (seed, mode). On any mismatch it prints the exact
// coordinates (agent index, fixture value, engine value, seed, mode) and
// exits non-zero.
//
// Linking: sample_fixture.cpp is compiled as a separate translation unit with
// -DSAMPLE_FIXTURE_AS_LIB (suppresses its main), so this test supplies the
// single main. The fixture's extern "C" rand()/srand() (MSVCRT LCG) becomes
// the process-global rand/srand; the engine never calls libc rand (it uses
// fidelity_rng()), so there is no double-binding.
// =============================================================================

#include "engine.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Declared by sample_fixture.cpp (compiled as SAMPLE_FIXTURE_AS_LIB).
// Re-runs the verbatim original tournament (MSVCRT stream) and writes the six
// float totalScores into totalScore, the per-match horizon draws into
// roundsDraws. distinct==false reproduces the original i<=j<=k loop.
void runSampleTournament(int seed, float totalScore[6],
                         std::vector<int>& roundsDraws, bool distinct);

using mas_sim::AgentBase;
using mas_sim::AgentFactory;
using mas_sim::RngKind;
using mas_sim::TournamentMode;
using mas_sim::fidelity_rng;
using mas_sim::run_tournament;

namespace {

constexpr int kNumAgents = 6;

// --- The six sample behaviors, re-implemented as engine AgentBase ---------
// Each stochastic agent draws from fidelity_rng() (the engine's MSVCRT LCG)
// at exactly the points where the original called rand().

class NiceAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                    std::vector<int>&) override {
    return 0;
  }
};

class NastyAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                    std::vector<int>&) override {
    return 1;
  }
};

class RandomAgent : public AgentBase {
 public:
  int selectAction(int, std::vector<int>&, std::vector<int>&,
                    std::vector<int>&) override {
    // original: rand() < RAND_MAX/2 ? 0 : 1  (MSVCRT RAND_MAX/2 = 16383)
    return fidelity_rng().rand() < mas_sim::kMsvcrtRandMax / 2 ? 0 : 1;
  }
};

class TolerantAgent : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    int opponentPeace = 0;
    int opponentWar = 0;
    for (int i = 0; i < n; i++)
      (opp1[i] == 0) ? ++opponentPeace : ++opponentWar;
    for (int i = 0; i < n; i++)
      (opp2[i] == 0) ? ++opponentPeace : ++opponentWar;
    return opponentWar > opponentPeace ? 1 : 0;
  }
};

class FreakyAgent : public AgentBase {
  int action_;
 public:
  // Constructor-side draw — must happen before the horizon draw, so the
  // factory constructs it before play_match() draws the horizon, exactly as
  // the original's FreakyAgent().
  FreakyAgent() {
    action_ = fidelity_rng().rand() < mas_sim::kMsvcrtRandMax / 2 ? 0 : 1;
  }

  int selectAction(int, std::vector<int>&, std::vector<int>&,
                   std::vector<int>&) override {
    return action_;
  }
};

class T4TAgent : public AgentBase {
 public:
  int selectAction(int n, std::vector<int>&, std::vector<int>& opp1,
                   std::vector<int>& opp2) override {
    if (n == 0) return 0;  // peace by default
    return fidelity_rng().rand() < mas_sim::kMsvcrtRandMax / 2
               ? opp1[n - 1]
               : opp2[n - 1];
  }
};

// Factory list in the sample's index order (0..5).
std::vector<AgentFactory> makeSampleFactories() {
  return {
      [] { return std::make_unique<NiceAgent>(); },
      [] { return std::make_unique<NastyAgent>(); },
      [] { return std::make_unique<RandomAgent>(); },
      [] { return std::make_unique<TolerantAgent>(); },
      [] { return std::make_unique<FreakyAgent>(); },
      [] { return std::make_unique<T4TAgent>(); },
  };
}

const char* modeName(TournamentMode m) {
  return (m == TournamentMode::kSelfInclusive) ? "selfInclusive" : "distinct";
}

// Bitwise comparison of two doubles (exact float-widening values).
bool sameBits(double a, double b) {
  return std::memcmp(&a, &b, sizeof(double)) == 0;
}

// Portable unsigned-64-bit view of a double's bits (for diagnostics).
uint64_t doubleBits(double d) {
  uint64_t u;
  std::memcpy(&u, &d, sizeof(u));
  return u;
}

}  // namespace

int main() {
  const int seeds[] = {1, 2, 3, 42, 12345, 777777};
  const TournamentMode modes[] = {TournamentMode::kSelfInclusive,
                                  TournamentMode::kDistinct};
  const std::vector<AgentFactory> factories = makeSampleFactories();

  int failures = 0;

  for (int seed : seeds) {
    for (TournamentMode mode : modes) {
      const bool distinct = (mode == TournamentMode::kDistinct);

      // (a) Fixture: original logic, MSVCRT stream, i<=j<=k or i<j<k.
      float fixtureScore[kNumAgents] = {0};
      std::vector<int> fixtureRounds;
      runSampleTournament(seed, fixtureScore, fixtureRounds, distinct);

      // (b) Engine: fidelity mode (MSVCRT LCG), same seed + agent behaviors.
      const std::vector<double> engineScore =
          run_tournament(factories, kNumAgents, seed, mode, RngKind::kMsvcrt);

      // (c) Bitwise per-agent equality.
      bool ok = (engineScore.size() == static_cast<std::size_t>(kNumAgents));
      if (ok) {
        for (int a = 0; a < kNumAgents; a++) {
          const double fixtureAsDouble =
              static_cast<double>(fixtureScore[a]);  // exact widening
          if (!sameBits(engineScore[a], fixtureAsDouble)) {
            std::printf(
                "      MISMATCH agent=%d seed=%d mode=%s: fixture=%.17g "
                "(0x%016llX) vs engine=%.17g (0x%016llX)\n",
                a, seed, modeName(mode), fixtureAsDouble,
                static_cast<unsigned long long>(doubleBits(fixtureAsDouble)),
                engineScore[a],
                static_cast<unsigned long long>(
                    doubleBits(engineScore[a])));
            ok = false;
          }
        }
      }

      if (ok) {
        std::printf("PASS seed=%d mode=%s\n", seed, modeName(mode));
      } else {
        std::printf("FAIL seed=%d mode=%s\n", seed, modeName(mode));
        ++failures;
      }
    }
  }

  if (failures == 0) {
    std::printf("ALL PASS (%d seed/mode combinations)\n",
                2 * static_cast<int>(sizeof(seeds) / sizeof(seeds[0])));
    return 0;
  }
  std::printf("FAIL: %d seed/mode combination(s) failed\n", failures);
  return 1;
}
