#!/usr/bin/env bash
# =============================================================================
# sim/build.sh — slice B1a: build the tournament engine library + self-test.
#
# Usage:
#   ./build.sh            # build -O2 lib + -O2 self-test, run self-test
#   ./build.sh debug      # also build & run the ASan/UBSan debug self-test
#   ./build.sh run        # run the -O2 self-test (assumes built)
#   ./build.sh campaign   # build campaign runner + self-test, run self-test
#   ./build.sh candidates   # build candidates (C1a) + tests, run tests
#   ./build.sh candidatesasan  # ASan/UBSan candidates tests
#   ./build.sh candidatese   # build candidate E (C1c: CandE_Hybrid) + tests
#   ./build.sh candidateseasan # ASan/UBSan candidate E tests
#   ./build.sh candidatesf   # build candidate F (C2a: CandF) + tests
#   ./build.sh candidatesfasan # ASan/UBSan candidate F tests
#   ./build.sh candidatesg   # build candidate G (C2d: CandG_TolRamp) + tests
#   ./build.sh candidatesgasan # ASan/UBSan candidate G tests
#
# No cmake: plain g++ invocations, one .a, two self-test binaries.
# =============================================================================
set -euo pipefail

cd "$(dirname "$0")"

CXX="${CXX:-g++}"
# -static-libgcc -static-libstdc++: this machine's MSYS2 install is missing
# shared-runtime import libraries (libgcc_s.dll.a confirmed absent), which
# breaks any link pulling emutls/TLS symbols (thread_local) against the shared
# runtimes. Static linking is semantically identical here; the proper repair
# is `pacman -S mingw-w64-ucrt-x86_64-gcc` (user action).
CXXFLAGS_OPT="-O2 -march=native -std=c++17 -Wall -Wextra -static-libgcc -static-libstdc++"
CXXFLAGS_DEBUG="-O0 -g -std=c++17 -Wall -Wextra -static-libgcc -static-libstdc++ -fsanitize=address,undefined -fno-omit-frame-pointer -DENGINE_SANITIZED"
BUILD_DIR="build"
mkdir -p "$BUILD_DIR"

build_opt() {
  echo "[build] -O2 engine lib + self-test"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine.o"
  $CXX $CXXFLAGS_OPT -c self_test_main.cpp -o "$BUILD_DIR/self_test.o" -DENGINE_TEST
  ar rcs "$BUILD_DIR/libengine.a" "$BUILD_DIR/engine.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/self_test.o" "$BUILD_DIR/libengine.a" \
      -o "$BUILD_DIR/self_test"
}

run_opt() {
  echo "[run] -O2 self-test"
  "$BUILD_DIR/self_test"
}

build_campaign() {
  echo "[build] -O2 campaign runner + self-test"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine.o"
  $CXX $CXXFLAGS_OPT -c campaign.cpp -o "$BUILD_DIR/campaign.o"
  ar rcs "$BUILD_DIR/libengine.a" "$BUILD_DIR/engine.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/campaign.o" "$BUILD_DIR/libengine.a" -pthread \
      -o "$BUILD_DIR/campaign"
  $CXX $CXXFLAGS_OPT -c campaign.cpp -o "$BUILD_DIR/campaign_test.o" -DCAMPAIGN_TEST
  $CXX $CXXFLAGS_OPT -c campaign_self_test_main.cpp -o "$BUILD_DIR/campaign_test_main.o" -DCAMPAIGN_TEST
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/campaign_test_main.o" "$BUILD_DIR/campaign_test.o" \
      "$BUILD_DIR/libengine.a" -pthread \
      -o "$BUILD_DIR/campaign_test"
}

run_campaign() {
  echo "[run] -O2 campaign self-test"
  "$BUILD_DIR/campaign_test"
}

build_debug() {
  echo "[build] -O0 -g ASan/UBSan self-test"
  $CXX $CXXFLAGS_DEBUG -c engine.cpp -o "$BUILD_DIR/engine_dbg.o"
  $CXX $CXXFLAGS_DEBUG -c self_test_main.cpp -o "$BUILD_DIR/self_test_dbg.o" -DENGINE_TEST
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/self_test_dbg.o" "$BUILD_DIR/engine_dbg.o" \
      -o "$BUILD_DIR/self_test_asan"
}

run_debug() {
  echo "[run] sanitizer self-test"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/self_test_asan"
}

# The `zoo` target links BOTH zoo TUs (t01: Tiers 0+1, t2: Tier 2) and builds
# one behavioral test binary per TU; each test binary pulls in both agent TUs
# so the full registered zoo is available for scripted-history and tournament
# checks.
build_zoo() {
  echo "[build] -O2 zoo (Tiers 0+1+2+3), behavioral tests"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine_zoo.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t2.cpp -o "$BUILD_DIR/zoo_t2.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t3.cpp -o "$BUILD_DIR/zoo_t3.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01_test.cpp -o "$BUILD_DIR/zoo_t01_test.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t2_test.cpp -o "$BUILD_DIR/zoo_t2_test.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t3_test.cpp -o "$BUILD_DIR/zoo_t3_test.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/zoo_t01_test.o" "$BUILD_DIR/zoo_t01.o" \
      "$BUILD_DIR/engine_zoo.o" -o "$BUILD_DIR/zoo_t01_test"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/zoo_t2_test.o" "$BUILD_DIR/zoo_t2.o" \
      "$BUILD_DIR/zoo_t01.o" "$BUILD_DIR/engine_zoo.o" -o "$BUILD_DIR/zoo_t2_test"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/zoo_t3_test.o" "$BUILD_DIR/zoo_t3.o" \
      "$BUILD_DIR/zoo_t2.o" "$BUILD_DIR/zoo_t01.o" "$BUILD_DIR/engine_zoo.o" \
      -o "$BUILD_DIR/zoo_t3_test"
}

run_zoo() {
  echo "[run] -O2 zoo behavioral tests (Tiers 0+1, 2, 3)"
  "$BUILD_DIR/zoo_t01_test"
  "$BUILD_DIR/zoo_t2_test"
  "$BUILD_DIR/zoo_t3_test"
}

# ASan/UBSan build of both zoo test binaries (sanitizer gate).
build_zoo_asan() {
  echo "[build] -O0 -g ASan/UBSan zoo tests"
  $CXX $CXXFLAGS_DEBUG -c engine.cpp -o "$BUILD_DIR/engine_zoo_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t2.cpp -o "$BUILD_DIR/zoo_t2_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t3.cpp -o "$BUILD_DIR/zoo_t3_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t01_test.cpp -o "$BUILD_DIR/zoo_t01_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t2_test.cpp -o "$BUILD_DIR/zoo_t2_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t3_test.cpp -o "$BUILD_DIR/zoo_t3_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/zoo_t01_test_dbg.o" "$BUILD_DIR/zoo_t01_dbg.o" \
      "$BUILD_DIR/engine_zoo_dbg.o" -o "$BUILD_DIR/zoo_t01_test_asan"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/zoo_t2_test_dbg.o" "$BUILD_DIR/zoo_t2_dbg.o" \
      "$BUILD_DIR/zoo_t01_dbg.o" "$BUILD_DIR/engine_zoo_dbg.o" \
      -o "$BUILD_DIR/zoo_t2_test_asan"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/zoo_t3_test_dbg.o" "$BUILD_DIR/zoo_t3_dbg.o" \
      "$BUILD_DIR/zoo_t2_dbg.o" "$BUILD_DIR/zoo_t01_dbg.o" \
      "$BUILD_DIR/engine_zoo_dbg.o" -o "$BUILD_DIR/zoo_t3_test_asan"
}

run_zoo_asan() {
  echo "[run] ASan/UBSan zoo behavioral tests"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/zoo_t01_test_asan"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/zoo_t2_test_asan"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/zoo_t3_test_asan"
}

build_fidelity() {
  echo "[build] -O2 fidelity gate (engine + fixture-as-lib + test)"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine_fidelity.o"
  $CXX $CXXFLAGS_OPT -I. -c fidelity/sample_fixture.cpp \
      -o "$BUILD_DIR/sample_fixture.o" -DSAMPLE_FIXTURE_AS_LIB
  $CXX $CXXFLAGS_OPT -I. -c fidelity/fidelity_test.cpp \
      -o "$BUILD_DIR/fidelity_test.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/fidelity_test.o" \
      "$BUILD_DIR/sample_fixture.o" "$BUILD_DIR/engine_fidelity.o" \
      -o "$BUILD_DIR/fidelity_test"
}

run_fidelity() {
  echo "[run] -O2 fidelity gate"
  "$BUILD_DIR/fidelity_test"
}


# The `candidates` target builds candidate CandA_GTFT + CandB_KLenient (slice
# C1a) plus their test binary. The test main links engine + zoo_t01 (anchor
# bots) + the candidates TU; the candidates TU itself only needs the engine
# for fast_rng() (zoo_common.hpp is included for the shared contract header,
# no zoo agent symbols are required).
build_candidates() {
  echo "[build] -O2 candidates (C1a) + test"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine_cand.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_ab.cpp \
      -o "$BUILD_DIR/candidates_ab.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_ab_test.cpp \
      -o "$BUILD_DIR/candidates_ab_test.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_cand.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/candidates_ab_test.o" \
      "$BUILD_DIR/candidates_ab.o" "$BUILD_DIR/zoo_t01_cand.o" \
      "$BUILD_DIR/engine_cand.o" -o "$BUILD_DIR/candidates_ab_test"
}

run_candidates() {
  echo "[run] -O2 candidates behavioral tests"
  "$BUILD_DIR/candidates_ab_test"
}

# ASan/UBSan build of the candidates test (sanitizer gate).
build_candidates_asan() {
  echo "[build] -O0 -g ASan/UBSan candidates test"
  $CXX $CXXFLAGS_DEBUG -c engine.cpp -o "$BUILD_DIR/engine_cand_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_ab.cpp \
      -o "$BUILD_DIR/candidates_ab_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_ab_test.cpp \
      -o "$BUILD_DIR/candidates_ab_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_cand_dbg.o"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/candidates_ab_test_dbg.o" \
      "$BUILD_DIR/candidates_ab_dbg.o" "$BUILD_DIR/zoo_t01_cand_dbg.o" \
      "$BUILD_DIR/engine_cand_dbg.o" -o "$BUILD_DIR/candidates_ab_test_asan"
}

run_candidates_asan() {
  echo "[run] ASan/UBSan candidates behavioral tests"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/candidates_ab_test_asan"
}


# The `candidatesc` target builds candidate C (C1b: CandC_Classifier) plus
# its test binary. The test main links engine + zoo_t01 (anchor bots) + the
# CandC TU; the CandC TU only needs the engine for fast_rng().
build_candidates_c() {
  echo "[build] -O2 candidates C (C1b: CandC_Classifier) + test"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine_candc.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_c.cpp \
      -o "$BUILD_DIR/candidates_c.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_c_test.cpp \
      -o "$BUILD_DIR/candidates_c_test.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candc.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/candidates_c_test.o" \
      "$BUILD_DIR/candidates_c.o" "$BUILD_DIR/zoo_t01_candc.o" \
      "$BUILD_DIR/engine_candc.o" -o "$BUILD_DIR/candidates_c_test"
}

run_candidates_c() {
  echo "[run] -O2 candidates C behavioral tests"
  "$BUILD_DIR/candidates_c_test"
}

# ASan/UBSan build of the CandC test (sanitizer gate).
build_candidates_c_asan() {
  echo "[build] -O0 -g ASan/UBSan candidates C test"
  $CXX $CXXFLAGS_DEBUG -c engine.cpp -o "$BUILD_DIR/engine_candc_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_c.cpp \
      -o "$BUILD_DIR/candidates_c_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_c_test.cpp \
      -o "$BUILD_DIR/candidates_c_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candc_dbg.o"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/candidates_c_test_dbg.o" \
      "$BUILD_DIR/candidates_c_dbg.o" "$BUILD_DIR/zoo_t01_candc_dbg.o" \
      "$BUILD_DIR/engine_candc_dbg.o" -o "$BUILD_DIR/candidates_c_test_asan"
}

run_candidates_c_asan() {
  echo "[run] ASan/UBSan candidates C behavioral tests"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/candidates_c_test_asan"
}

# The `candidatesc` target builds candidate E (C1c: CandE_Hybrid, the hybrid
# ship candidate) plus its test binary. The test main links engine +
# zoo_t01 (anchor bots) + the CandE TU; the CandE TU only needs the engine
# for fast_rng() (zoo_common.hpp is included for the shared contract header).
build_candidates_e() {
  echo "[build] -O2 candidates E (C1c: CandE_Hybrid) + test"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine_candc.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_e.cpp \
      -o "$BUILD_DIR/candidates_e.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_e_test.cpp \
      -o "$BUILD_DIR/candidates_e_test.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candc.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/candidates_e_test.o" \
      "$BUILD_DIR/candidates_e.o" "$BUILD_DIR/zoo_t01_candc.o" \
      "$BUILD_DIR/engine_candc.o" -o "$BUILD_DIR/candidates_e_test"
}

run_candidates_e() {
  echo "[run] -O2 candidates E behavioral tests"
  "$BUILD_DIR/candidates_e_test"
}

# ASan/UBSan build of the CandE test (sanitizer gate).
build_candidates_e_asan() {
  echo "[build] -O0 -g ASan/UBSan candidates E test"
  $CXX $CXXFLAGS_DEBUG -c engine.cpp -o "$BUILD_DIR/engine_candc_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_e.cpp \
      -o "$BUILD_DIR/candidates_e_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_e_test.cpp \
      -o "$BUILD_DIR/candidates_e_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candc_dbg.o"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/candidates_e_test_dbg.o" \
      "$BUILD_DIR/candidates_e_dbg.o" "$BUILD_DIR/zoo_t01_candc_dbg.o" \
      "$BUILD_DIR/engine_candc_dbg.o" -o "$BUILD_DIR/candidates_e_test_asan"
}

run_candidates_e_asan() {
  echo "[run] ASan/UBSan candidates E behavioral tests"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/candidates_e_test_asan"
}

# The `candidatesf` target builds candidate F (C2a: CandF — the
# endgame-ramp retaliator) plus its test binary. The test main links
# engine + zoo_t01 (anchor bots: Nice/Nasty/Random/TFT_any) + the CandF TU;
# the CandF TU only needs the engine for fast_rng() (zoo_common.hpp is
# included for the shared contract header). Mirrors the candidatese target.
build_candidates_f() {
  echo "[build] -O2 candidates F (C2a: CandF) + test"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine_candf.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_f.cpp \
      -o "$BUILD_DIR/candidates_f.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_f_test.cpp \
      -o "$BUILD_DIR/candidates_f_test.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candf.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/candidates_f_test.o" \
      "$BUILD_DIR/candidates_f.o" "$BUILD_DIR/zoo_t01_candf.o" \
      "$BUILD_DIR/engine_candf.o" -o "$BUILD_DIR/candidates_f_test"
}

run_candidates_f() {
  echo "[run] -O2 candidates F behavioral tests"
  "$BUILD_DIR/candidates_f_test"
}

# ASan/UBSan build of the CandF test (sanitizer gate).
build_candidates_f_asan() {
  echo "[build] -O0 -g ASan/UBSan candidates F test"
  $CXX $CXXFLAGS_DEBUG -c engine.cpp -o "$BUILD_DIR/engine_candf_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_f.cpp \
      -o "$BUILD_DIR/candidates_f_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_f_test.cpp \
      -o "$BUILD_DIR/candidates_f_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candf_dbg.o"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/candidates_f_test_dbg.o" \
      "$BUILD_DIR/candidates_f_dbg.o" "$BUILD_DIR/zoo_t01_candf_dbg.o" \
      "$BUILD_DIR/engine_candf_dbg.o" -o "$BUILD_DIR/candidates_f_test_asan"
}

run_candidates_f_asan() {
  echo "[run] ASan/UBSan candidates F behavioral tests"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/candidates_f_test_asan"
}

# The `candidatesg` target builds candidate G (C2d: CandG_TolRamp — the
# "coast + harvest" hybrid: zoo TolerantW5 backbone + committed endgame war
# ramp) plus its test binary. The test main links engine + zoo_t01 (the
# TolerantW5 reference + anchor bots) + the CandG TU; the CandG TU only
# needs the engine contract (zoo_common.hpp). Mirrors the candidatesf
# target exactly.
build_candidates_g() {
  echo "[build] -O2 candidates G (C2d: CandG_TolRamp) + test"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine_candg.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_g.cpp \
      -o "$BUILD_DIR/candidates_g.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_g_test.cpp \
      -o "$BUILD_DIR/candidates_g_test.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candg.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/candidates_g_test.o" \
      "$BUILD_DIR/candidates_g.o" "$BUILD_DIR/zoo_t01_candg.o" \
      "$BUILD_DIR/engine_candg.o" -o "$BUILD_DIR/candidates_g_test"
}

run_candidates_g() {
  echo "[run] -O2 candidates G behavioral tests"
  "$BUILD_DIR/candidates_g_test"
}

# ASan/UBSan build of the CandG test (sanitizer gate).
build_candidates_g_asan() {
  echo "[build] -O0 -g ASan/UBSan candidates G test"
  $CXX $CXXFLAGS_DEBUG -c engine.cpp -o "$BUILD_DIR/engine_candg_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_g.cpp \
      -o "$BUILD_DIR/candidates_g_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -Izoo -c candidates/candidates_g_test.cpp \
      -o "$BUILD_DIR/candidates_g_test_dbg.o"
  $CXX $CXXFLAGS_DEBUG -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01_candg_dbg.o"
  $CXX $CXXFLAGS_DEBUG "$BUILD_DIR/candidates_g_test_dbg.o" \
      "$BUILD_DIR/candidates_g_dbg.o" "$BUILD_DIR/zoo_t01_candg_dbg.o" \
      "$BUILD_DIR/engine_candg_dbg.o" -o "$BUILD_DIR/candidates_g_test_asan"
}

run_candidates_g_asan() {
  echo "[run] ASan/UBSan candidates G behavioral tests"
  ASAN_OPTIONS=detect_leaks=0 "$BUILD_DIR/candidates_g_test_asan"
}

# `campaign_full` builds the production screening binary: campaign runner
# linked with the FULL registry (zoo Tiers 0-3 + all candidate arms).
build_campaign_full() {
  echo "[build] -O2 campaign_full (engine + zoo 0-3 + candidates A/B/C/E + runner)"
  $CXX $CXXFLAGS_OPT -c engine.cpp -o "$BUILD_DIR/engine.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t01.cpp -o "$BUILD_DIR/zoo_t01.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t2.cpp -o "$BUILD_DIR/zoo_t2.o"
  $CXX $CXXFLAGS_OPT -I. -c zoo/zoo_t3.cpp -o "$BUILD_DIR/zoo_t3.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_ab.cpp -o "$BUILD_DIR/candidates_ab.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_c.cpp -o "$BUILD_DIR/candidates_c.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_e.cpp -o "$BUILD_DIR/candidates_e.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_f.cpp -o "$BUILD_DIR/candidates_f.o"
  $CXX $CXXFLAGS_OPT -I. -Izoo -c candidates/candidates_g.cpp -o "$BUILD_DIR/candidates_g.o"
  $CXX $CXXFLAGS_OPT -I. -c campaign.cpp -o "$BUILD_DIR/campaign.o"
  $CXX $CXXFLAGS_OPT "$BUILD_DIR/campaign.o" "$BUILD_DIR/candidates_g.o" \
      "$BUILD_DIR/candidates_f.o" \
      "$BUILD_DIR/candidates_e.o" \
      "$BUILD_DIR/candidates_c.o" "$BUILD_DIR/candidates_ab.o" \
      "$BUILD_DIR/zoo_t3.o" "$BUILD_DIR/zoo_t2.o" "$BUILD_DIR/zoo_t01.o" \
      "$BUILD_DIR/engine.o" -pthread -o "$BUILD_DIR/campaign_full"
}

run_campaign_full() {
  echo "[build] campaign_full linked (smoke: run a 1-run tiny manifest manually)"
}

case "${1:-all}" in
  all)            build_opt; run_opt ;;
  opt)            build_opt; run_opt ;;
  run)            run_opt ;;
  debug)          build_opt; run_opt; build_debug; run_debug ;;
  fidelity)       build_fidelity; run_fidelity ;;
  campaign)       build_campaign; run_campaign ;;
  campaign_full)  build_campaign_full; run_campaign_full ;;
  zoo)            build_zoo; run_zoo ;;
  zooasan)        build_zoo_asan; run_zoo_asan ;;
  candidates)     build_candidates; run_candidates ;;
  candidatesasan) build_candidates_asan; run_candidates_asan ;;
  candidatesc)    build_candidates_c; run_candidates_c ;;
  candidatescasan) build_candidates_c_asan; run_candidates_c_asan ;;
  candidatese)    build_candidates_e; run_candidates_e ;;
  candidateseasan) build_candidates_e_asan; run_candidates_e_asan ;;
  candidatesf)    build_candidates_f; run_candidates_f ;;
  candidatesfasan) build_candidates_f_asan; run_candidates_f_asan ;;
  candidatesg)    build_candidates_g; run_candidates_g ;;
  candidatesgasan) build_candidates_g_asan; run_candidates_g_asan ;;
  *) echo "usage: $0 [all|opt|run|debug|fidelity|campaign|campaign_full|zoo|zooasan|candidates|candidatesasan|candidatesc|candidatescasan|candidatese|candidateseasan|candidatesf|candidatesfasan|candidatesg|candidatesgasan]" >&2; exit 2 ;;
esac
