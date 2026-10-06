// =============================================================================
// sim/self_test_main.cpp — tiny entry point for the engine self-test.
//
// Build with -DENGINE_TEST (see sim/build.sh). Prints PASS/FAIL detail and
// exits 0 on success, 1 on failure.
// =============================================================================

#ifdef ENGINE_TEST

#include "engine.hpp"

int main() { return mas_sim::engine_self_test(); }

#endif  // ENGINE_TEST
