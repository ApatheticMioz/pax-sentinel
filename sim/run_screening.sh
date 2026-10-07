#!/usr/bin/env bash
# =============================================================================
# run_screening.sh — screening campaign driver (POOLS.md section 3).
#
# Usage:
#   ./run_screening.sh <family> [runs] [seed] [arms...]
#     family: pools/<family>.txt (prior, nice-heavy, ...)
#     arms:   default "CandE_Hybrid CandA_GTFT CandB_KLenient CandC_Classifier"
#
# Builds an arm manifest per arm (arm at slot 0, family opponents after —
# identical composition across arms), then runs campaign_full sequentially
# with all cores. Same --seed across arms => run i uses identical seed streams
# (CRN pairing at the run level) and identical opponent composition/order.
#
# Output: results/screening/<family>_<arm>.jsonl (one JSON line per run).
# --rng fast (sfc64): screening speed path. Final freeze validation reruns the
# winner with --rng fidelity (bitwise-locked to the original MSVCRT harness).
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

FAMILY="${1:?usage: run_screening.sh <family> [runs] [seed] [arms...]}"
RUNS="${2:-5000}"
SEED="${3:-731842}"
shift 3 2>/dev/null || true
if [ "$#" -gt 0 ]; then ARMS=("$@"); else ARMS=(CandE_Hybrid CandA_GTFT CandB_KLenient CandC_Classifier); fi

mkdir -p results/screening pools/screening

for ARM in "${ARMS[@]}"; do
  MAN="pools/screening/${FAMILY}_${ARM}.txt"
  { echo "$ARM"; cat "pools/${FAMILY}.txt"; } > "$MAN"
  OUT="results/screening/${FAMILY}_${ARM}.jsonl"
  echo "[screen] $FAMILY / $ARM -> $OUT"
  RESUME=""
  [ -f "$OUT" ] && RESUME="--resume"
  ./build/campaign_full --pool "$MAN" --seed "$SEED" --mode distinct \
      --runs "$RUNS" --threads 16 --rng fast --out "$OUT" $RESUME
done
echo "[screen] done: $FAMILY"
