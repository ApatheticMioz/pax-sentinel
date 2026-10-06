// =============================================================================
// sim/campaign_self_test_main.cpp — entry point for the campaign self-test.
//
// Build with -DCAMPAIGN_TEST (see sim/build.sh). Runs the builtin-bot
// registry/determinism self-test (campaign_self_test in campaign.cpp) and
// exits 0 on success, 1 on failure.
// =============================================================================

#ifdef CAMPAIGN_TEST

#include "engine.hpp"

namespace mas_sim {
int campaign_self_test();
}  // namespace mas_sim

int main() { return mas_sim::campaign_self_test(); }

#endif  // CAMPAIGN_TEST
