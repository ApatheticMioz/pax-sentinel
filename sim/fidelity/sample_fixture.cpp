// =============================================================================
// sim/fidelity/sample_fixture.cpp — slice B1b: end-to-end fidelity gate
//
// A SELF-CONTAINED, LOGIC-VERBATIM copy of the original ThreeWayPeaceWar.cpp
// runner + its six sample agents (Nice / Nasty / Random / Tolerant / Freaky /
// T4T). The only sanctioned deviations from the original source are:
//
//   (1) `main(argc, argv)` takes a FIXED seed as argv[1] and calls
//       srand(seed) instead of srand(time(NULL)).
//   (2) Instead of printing the sorted human-readable summary, it prints each
//       agent's final totalScore with full precision (printf %.9g) and the
//       per-match rounds-draw sequence.
//
// Every other line of tournament logic is preserved verbatim: the payoff
// tensor, the per-match construct-A/B/C-then-draw-horizon ordering (so
// FreakyAgent's constructor RNG draw happens before the horizon draw), the
// per-round A->B->C selectAction call order, extendIntArray semantics, and
// the sample-faithful scoring types (`float totalScore[] += matchResults[x]
// / rounds`).
//
// ---------------------------------------------------------------------------
// FIDELITY RNG (README.md gotcha #2)
// ---------------------------------------------------------------------------
// The original was written for the MSVC CRT, whose rand() is a 32-bit LCG
// (seed = seed*214013 + 2531011; out = (seed>>16)&0x7FFF) with RAND_MAX =
// 0x7FFF. On a non-MSVC host glibc's rand() is a different algorithm with
// RAND_MAX = 2^31-1, so `rand()` / `RAND_MAX` would NOT reproduce the MSVCRT
// stream. To keep this fixture a *logic-verbatim* copy of the original while
// making its stream bit-exact with the engine's MsvcrtRng, we rebind the
// global C rand()/srand() (that the agents and the horizon formula call
// verbatim) to the MSVCRT LCG, and redefine RAND_MAX to 0x7FFF. The agents
// still literally call rand() and RAND_MAX — the only thing that changes is
// which stream those symbols resolve to at link time.
// =============================================================================

#include <iostream>
#include <vector>
#include <cmath>
#include <cxxabi.h>
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <cstdlib>

// --- MSVCRT-compatible 32-bit LCG, overriding glibc's rand()/srand() ------
namespace {
unsigned int g_msvcrt_seed = 0;
}  // namespace

extern "C" void srand(unsigned int seed) { g_msvcrt_seed = seed; }
extern "C" int rand(void) {
  g_msvcrt_seed = g_msvcrt_seed * 214013u + 2531011u;
  return static_cast<int>((g_msvcrt_seed >> 16) & 0x7FFFu);
}
#undef RAND_MAX
#define RAND_MAX 0x7FFF  // MSVCRT's RAND_MAX — part of the bit-exact contract

// ---------------------------------------------------------------------------
// Verbatim copy of the original class body.
// ---------------------------------------------------------------------------
class ThreeWayPeaceWar {
    // Payoff matrix
    inline static constexpr int payoff[2][2][2] = {
        {{4, 1}, {1, -1}},
        {{6, 3}, {3, 0}}
    };

    class Agent {
    public:
        virtual int selectAction(int n, std::vector<int>& myHistory,
                                 std::vector<int>& oppHistory1,
                                 std::vector<int>& oppHistory2) = 0;

        virtual ~Agent() = default;   // fixes the UB on delete (see below)

        std::string name() const {
            int status = 0;
            char* demangled = abi::__cxa_demangle(typeid(*this).name(), nullptr, nullptr, &status);
            std::string full = (status == 0 && demangled) ? demangled : typeid(*this).name();
            std::free(demangled);

            // full looks like "ThreeWayPeaceWar::NiceAgent" — strip the enclosing scope
            auto pos = full.rfind("::");
            return (pos == std::string::npos) ? full : full.substr(pos + 2);
        }
    };

    class NiceAgent : public Agent {
    public:
        int selectAction(int n, std::vector<int>& myHistory, std::vector<int>& oppHistory1, std::vector<int>& oppHistory2) override {
            return 0;
        }
    };

    class NastyAgent : public Agent {
    public:
        int selectAction(int n, std::vector<int>& myHistory, std::vector<int>& oppHistory1, std::vector<int>& oppHistory2) override {
            return 1;
        }
    };

    class RandomAgent : public Agent {
    public:
        int selectAction(int n, std::vector<int>& myHistory, std::vector<int>& oppHistory1, std::vector<int>& oppHistory2) override {
            return rand() < RAND_MAX / 2 ? 0 : 1;
        }
    };

    class TolerantAgent : public Agent {
    public:
        int selectAction(int n, std::vector<int>& myHistory, std::vector<int>& oppHistory1, std::vector<int>& oppHistory2) override {
            int opponentPeace = 0;
            int opponentWar = 0;
            for (int i = 0; i < n; i++) {
                if (oppHistory1[i] == 0)
                    opponentPeace++;
                else
                    opponentWar++;
            }
            for (int i = 0; i < n; i++) {
                if (oppHistory2[i] == 0)
                    opponentPeace++;
                else
                    opponentWar++;
            }
            return opponentWar > opponentPeace ? 1 : 0;
        }
    };

    class FreakyAgent : public Agent {
        int action;

    public:
        FreakyAgent() {
            action = rand() < RAND_MAX / 2 ? 0 : 1;
        }

        int selectAction(int n, std::vector<int>& myHistory, std::vector<int>& oppHistory1, std::vector<int>& oppHistory2) override {
            return action;
        }
    };

    class T4TAgent : public Agent {
    public:
        int selectAction(int n, std::vector<int>& myHistory, std::vector<int>& oppHistory1, std::vector<int>& oppHistory2) override {
            if (n == 0)
                return 0; // peace by default
            return rand() < RAND_MAX / 2 ? oppHistory1[n - 1] : oppHistory2[n - 1];
        }
    };

    std::vector<int> extendIntArray(std::vector<int>& arr, int next) {
        arr.push_back(next);
        return arr;
    }

public:
    static constexpr int numAgents = 6;

    Agent* makeAgent(int which) {
        switch (which) {
        case 0: return new NiceAgent();
        case 1: return new NastyAgent();
        case 2: return new RandomAgent();
        case 3: return new TolerantAgent();
        case 4: return new FreakyAgent();
        case 5: return new T4TAgent();
        default: throw "Bad argument passed to makeAgent";
        }
    }

    // Verbatim tournament body. The ONLY additions are:
    //   * `distinct` selects the enumeration bounds (false == the original
    //     i<=j<=k loop; true == i<j<k, the engine's documented kDistinct).
    //     The self-inclusive path is byte-for-byte the original loop.
    //   * `totalScore` is written to the caller's buffer instead of being
    //     a local (the value is identical either way).
    //   * each drawn `rounds` is appended to `roundsDraws`.
    //   * the terminal sorted-order print is replaced by the caller (main /
    //     fidelity_test).
    void runTournament(float totalScore[numAgents], std::vector<int>& roundsDraws,
                       bool distinct) {
        for (int i = 0; i < numAgents; i++) {
            for (int j = (distinct ? i + 1 : i); j < numAgents; j++) {
                for (int k = (distinct ? j + 1 : j); k < numAgents; k++) {
                    Agent* A = makeAgent(i);
                    Agent* B = makeAgent(j);
                    Agent* C = makeAgent(k);
                    int rounds = 90 + round(20 * (float)rand() / RAND_MAX); // Between 90 and 110 rounds
                    roundsDraws.push_back(rounds);
                    float matchResults[3];
                    matchResults[0] = 0;
                    matchResults[1] = 0;
                    matchResults[2] = 0;
                    std::vector<int> HistoryA, HistoryB, HistoryC;

                    for (int l = 0; l < rounds; l++) {
                        int PlayA = A->selectAction(l, HistoryA, HistoryB, HistoryC);
                        int PlayB = B->selectAction(l, HistoryB, HistoryC, HistoryA);
                        int PlayC = C->selectAction(l, HistoryC, HistoryA, HistoryB);
                        matchResults[0] += payoff[PlayA][PlayB][PlayC];
                        matchResults[1] += payoff[PlayB][PlayC][PlayA];
                        matchResults[2] += payoff[PlayC][PlayA][PlayB];
                        HistoryA = extendIntArray(HistoryA, PlayA);
                        HistoryB = extendIntArray(HistoryB, PlayB);
                        HistoryC = extendIntArray(HistoryC, PlayC);
                    }

                    totalScore[i] += matchResults[0] / rounds;
                    totalScore[j] += matchResults[1] / rounds;
                    totalScore[k] += matchResults[2] / rounds;

                    delete A;
                    delete B;
                    delete C;
                }
            }
        }
    }
};

// Public entry point used by both main() (stdout contract) and
// fidelity_test.cpp (bitwise comparison). `distinct == false` reproduces the
// original ThreeWayPeaceWar.cpp loop exactly.
void runSampleTournament(int seed, float totalScore[ThreeWayPeaceWar::numAgents],
                         std::vector<int>& roundsDraws, bool distinct) {
    srand(seed);  // fixed caller-provided seed (the original used time(NULL))
    ThreeWayPeaceWar instance;
    instance.runTournament(totalScore, roundsDraws, distinct);
}

// The fixture is normally a standalone runner (build.sh `fixture` target).
// When compiled into the fidelity_test binary it is built with
// -DSAMPLE_FIXTURE_AS_LIB, which suppresses this main (the test supplies its
// own) but keeps runSampleTournament() available for linking.
#ifndef SAMPLE_FIXTURE_AS_LIB
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <seed>\n", argv[0]);
        return 2;
    }
    const int seed = std::atoi(argv[1]);

    float totalScore[ThreeWayPeaceWar::numAgents] = {0};
    std::vector<int> roundsDraws;
    runSampleTournament(seed, totalScore, roundsDraws, /*distinct=*/false);

    // Full-precision per-agent totals (float widened exactly to double).
    for (int i = 0; i < ThreeWayPeaceWar::numAgents; i++) {
        std::printf("agent %d: %.9g\n", i, (double)totalScore[i]);
    }

    // The rounds-draw sequence (one entry per match, in tournament order).
    std::printf("rounds:");
    for (int r : roundsDraws) std::printf(" %d", r);
    std::printf("\n");

    return 0;
}
#endif  // SAMPLE_FIXTURE_AS_LIB
