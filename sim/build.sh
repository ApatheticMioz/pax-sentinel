#!/usr/bin/env bash
# =============================================================================
# sim/build.sh — slice B1a: build the tournament engine library + self-test.
#
# Usage:
#   ./build.sh            # build -O2 lib + -O2 self-test, run self-test
#   ./build.sh debug      # also build & run the ASan/UBSan debug self-test
#   ./build.sh run        # run the -O2 self-test (assumes built)
#
# No cmake: plain g++ invocations, one .a, two self-test binaries.
# =============================================================================
set -euo pipefail

cd "$(dirname "$0")"

CXX="${CXX:-g++}"
CXXFLAGS_OPT="-O2 -march=native -std=c++17 -Wall -Wextra"
CXXFLAGS_DEBUG="-O0 -g -std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -DENGINE_SANITIZED"
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

case "${1:-all}" in
  all)     build_opt; run_opt ;;
  opt)     build_opt; run_opt ;;
  run)     run_opt ;;
  debug)   build_opt; run_opt; build_debug; run_debug ;;
  *) echo "usage: $0 [all|opt|run|debug]" >&2; exit 2 ;;
esac
