#include <iostream>
#include <vector>
#include <cmath>
#include <cxxabi.h> 
#include <cstdlib>
#include <ctime>

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

    // RENAME: <student-id><Name>Agent before submission.
    //
    // Design anchors (sim/): the CTFT contrition backbone (Wu & Axelrod,
    // "How to cope with noise in the iterated prisoner's dilemma",
    // Journal of Conflict Resolution 39(1):183-189, 1995); the endgame
    // unraveling ramp (a committed endgame war at a fixed threshold,
    // because any forgiving agent can be undercut in the last few rounds);
    // and the 28k CRN-run screening that froze this configuration.
    //
    // Decision path (evaluated top-down every round; first match decides),
    // a faithful port of the frozen CandF_T104 (ramp_mode = 1, ramp_T = 104;
    // deterministic — no randomness, no member state, O(1) per round):
    //   R0  n == 0                      -> peace (open peace).
    //   R2  both opponents warred last  -> war (unconditional override;
    //       fires before the contrite/forgiveness path and the ramp).
    //   R3  n >= 90 (window) && n >= 104-> war (the committed ramp; once
    //       in play the CTFT contrite override can no longer fire inside
    //       the window, so the ramp is never suppressed).
    //   R1  (backbone) my last was war while BOTH opponents peaced -> peace
    //       (the contrite response to an unprovoked war); else mirror war
    //       if either opponent warred last round (TFT_any); else peace.
    class RampSentinelAgent : public Agent {
        static constexpr int kRampWindowStart = 90;  // the ramp window opens at the minimum horizon
        static constexpr int kRampThreshold = 104;   // the frozen threshold: committed war from round 104

    public:
        int selectAction(int n, std::vector<int>& myHistory, std::vector<int>& oppHistory1, std::vector<int>& oppHistory2) override {
            if (n == 0)
                return 0; // R0: open peace.
            if (oppHistory1[n - 1] == 1 && oppHistory2[n - 1] == 1)
                return 1; // R2: unconditional both-opponents-war override.
            if (n >= kRampWindowStart && n >= kRampThreshold)
                return 1; // R3: committed endgame ramp (the 104 threshold binds).
            if (myHistory[n - 1] == 1 && oppHistory1[n - 1] == 0 && oppHistory2[n - 1] == 0)
                return 0; // R1: contrite (unprovoked self-war last round).
            return (oppHistory1[n - 1] == 1 || oppHistory2[n - 1] == 1) ? 1 : 0; // R1: TFT_any mirror.
        }
    };

    std::vector<int> extendIntArray(std::vector<int>& arr, int next) {
        arr.push_back(next);
        return arr;
    }

public:
    static constexpr int numAgents = 7; // was 6; +1 for RampSentinelAgent (kept in sync with makeAgent below)

    Agent* makeAgent(int which) {
        switch (which) {
        case 0: return new NiceAgent();
        case 1: return new NastyAgent();
        case 2: return new RandomAgent();
        case 3: return new TolerantAgent();
        case 4: return new FreakyAgent();
        case 5: return new T4TAgent();
        case 6: return new RampSentinelAgent();
        default: throw "Bad argument passed to makeAgent"; // an out-of-range index is impossible with the numAgents roster above
        }
    }

    void runTournament() {
        float totalScore[numAgents] = {0};

        for (int i = 0; i < numAgents; i++) {
            for (int j = i; j < numAgents; j++) {
                for (int k = j; k < numAgents; k++) {
                    Agent* A = makeAgent(i);
                    Agent* B = makeAgent(j);
                    Agent* C = makeAgent(k);
                    int rounds = 90 + round(20 * (float)rand() / RAND_MAX); // Between 90 and 110 rounds
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

                   // std::cout << A->name() << " scored " << matchResults[0] / rounds << " points, "
                    //          << B->name() << " scored " << matchResults[1] / rounds << " points, and "
                     //         << C->name() << " scored " << matchResults[2] / rounds << " points." << std::endl;

                    delete A;
                    delete B;
                    delete C;
                }
            }
        }

        int sortedOrder[numAgents];
	for (int i = 0; i < numAgents; i++) {
	    int key = i;
	    int j = i - 1;
	    while (j >= 0 && totalScore[sortedOrder[j]] < totalScore[key]) {
		sortedOrder[j + 1] = sortedOrder[j];
		j--;
	    }
	    sortedOrder[j + 1] = key;
	}

        std::cout << "Tournament Results" << std::endl;
        for (int i = 0; i < numAgents; i++) {
            Agent* a = makeAgent(sortedOrder[i]);
	    std::cout << a->name() << ": " << totalScore[sortedOrder[i]] << " points." << std::endl;
	    delete a;
        }
    }
};


int main() {
    srand(time(NULL)); // Seed for random number generation
    ThreeWayPeaceWar instance;
    instance.runTournament();
    return 0;
}

