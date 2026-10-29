import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import argparse
import json
import os
import time
from .tournament import run_tournament_series, CONFIG_NAMES

def run_grand_championship(matches_per_tournament=20, base_seed=42000, log_dir="sim/arena/logs"):
    os.makedirs(log_dir, exist_ok=True)
    t_grand_start = time.time()

    print("=" * 95)
    print("AI4012 THREE-WAY PEACE-WAR: GRAND ARENA CHAMPIONSHIP")
    print(f"Total Tournaments: 4 | Matches Per Tournament: {matches_per_tournament} | Total Matches: {4 * matches_per_tournament}")
    print(f"Horizon per Match: Uniform[90, 110] rounds | Cross-Match Memory: None (Strict Fresh Agent Construction)")
    print(f"Concurrency: Max 2 Concurrent vLLM Calls (Enforced by Global Semaphore)")
    print("=" * 95)

    grand_results = {}

    for cfg_id in [1, 2, 3, 4]:
        cfg_name = CONFIG_NAMES[cfg_id]
        print("\n" + "#" * 95)
        print(f"STARTING TOURNAMENT {cfg_id}/4: {cfg_name}")
        print("#" * 95)

        cfg_seed = base_seed + cfg_id * 1000
        series_res = run_tournament_series(
            config_id=cfg_id,
            num_matches=matches_per_tournament,
            base_seed=cfg_seed,
            rounds_per_match=None,
            log_dir=log_dir
        grand_results[f"tournament_{cfg_id}"] = series_res

        # Save intermediate grand championship summary after each completed tournament
        grand_file = os.path.join(log_dir, "grand_championship_summary.json")
        with open(grand_file, "w", encoding="utf-8") as f:
            json.dump({
                "matches_per_tournament": matches_per_tournament,
                "current_progress": f"{cfg_id}/4 tournaments completed",
                "total_elapsed_sec": time.time() - t_grand_start,
                "tournaments": grand_results
            }, f, indent=2)

    total_grand_time = time.time() - t_grand_start

    print("\n" + "=" * 95)
    print("GRAND ARENA CHAMPIONSHIP CONCLUDED")
    print(f"Total Runtime: {total_grand_time:.1f}s ({total_grand_time/3600:.2f} hours)")
    print("=" * 95)

    for cfg_id in [1, 2, 3, 4]:
        s = grand_results[f"tournament_{cfg_id}"]
        cname = s["config_name"]
        print(f"\n--- Standings: {cname} ---")
        for i in range(3):
            win_pct = (s["win_counts"][i] / matches_per_tournament) * 100.0
            print(f"  {s['players'][i]:<25} | P(Win): {win_pct:>5.1f}% | Avg Match Score: {s['match_averages'][i]:>6.2f} pts | Per-Rd Avg: {s['overall_averages'][i]:>5.3f}")
        print(f"  Series Winner(s): {', '.join(s['champions'])}")

    grand_file = os.path.join(log_dir, "grand_championship_summary.json")
    with open(grand_file, "w", encoding="utf-8") as f:
        json.dump({
            "matches_per_tournament": matches_per_tournament,
            "total_runtime_sec": total_grand_time,
            "tournaments": grand_results
        }, f, indent=2)
    print(f"\nGrand summary persisted to {grand_file}\n")

def main():
    parser = argparse.ArgumentParser(description="Grand Arena Championship Orchestrator")
    parser.add_argument("--matches", type=int, default=20,
                        help="Number of matches per tournament (default: 20)")
    parser.add_argument("--seed", type=int, default=42000,
                        help="Base seed for reproducibility")
    parser.add_argument("--log-dir", type=str, default="sim/arena/logs",
                        help="Output directory for match and series JSON logs")
    args = parser.parse_args()

    run_grand_championship(matches_per_tournament=args.matches, base_seed=args.seed, log_dir=args.log_dir)

if __name__ == "__main__":
    main()
