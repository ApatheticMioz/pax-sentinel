import os
import json
from collections import defaultdict

def run_comparison(dir_a="sim/arena/logs", dir_b="sim/arena/logs_meta", out_file="sim/arena/logs/comparative_conditions_summary.json"):
    grand_a_path = os.path.join(dir_a, "grand_championship_summary.json")
    grand_b_path = os.path.join(dir_b, "grand_championship_summary.json")

    if not os.path.exists(grand_a_path) or not os.path.exists(grand_b_path):
        print(f"Error: Missing summary files ({grand_a_path} or {grand_b_path})")
        return

    with open(grand_a_path, "r", encoding="utf-8") as f:
        ga = json.load(f)
    with open(grand_b_path, "r", encoding="utf-8") as f:
        gb = json.load(f)

    comparison = {
        "title": "Grand Championship Comparative Analysis: Condition A (Amnesiac) vs Condition B (Meta-State Aware)",
        "condition_a": {"dir": dir_a, "total_runtime_sec": ga.get("total_runtime_sec", 0)},
        "condition_b": {"dir": dir_b, "total_runtime_sec": gb.get("total_runtime_sec", 0)},
        "series_comparisons": {}
    }

    print("=" * 100)
    print("GRAND ARENA CHAMPIONSHIP COMPARATIVE SUMMARY (Condition A vs. Condition B)")
    print("=" * 100)
    print(f"{'Series / Configuration':<42} | {'Player':<22} | {'Cond A':<11} | {'Cond B':<11} | {'Win Delta':<10} | {'Score Delta':<11}")
    print("-" * 120)

    total_wins_a = defaultdict(int)
    total_wins_b = defaultdict(int)

    for cfg_id in range(1, 5):
        t_key = f"tournament_{cfg_id}"
        ta = ga["tournaments"][t_key]
        tb = gb["tournaments"][t_key]
        cname = ta["config_name"]

        series_data = {
            "config_id": cfg_id,
            "config_name": cname,
            "players": ta["players"],
            "comparisons": []
        }

        for i, p in enumerate(ta["players"]):
            wa = ta["win_counts"][i]
            wb = tb["win_counts"][i]
            sa = ta["total_scores"][i]
            sb = tb["total_scores"][i]
            wd = wb - wa
            sd = sb - sa

            total_wins_a[p] += wa
            total_wins_b[p] += wb

            series_data["comparisons"].append({
                "player": p,
                "wins_a": wa,
                "win_pct_a": wa / ta["num_matches"] * 100,
                "score_a": sa,
                "avg_match_score_a": sa / ta["num_matches"],
                "wins_b": wb,
                "win_pct_b": wb / tb["num_matches"] * 100,
                "score_b": sb,
                "avg_match_score_b": sb / tb["num_matches"],
                "win_delta": wd,
                "score_delta": sd
            })

            w_str_a = f"{wa}/{ta['num_matches']} ({wa*5}%)"
            w_str_b = f"{wb}/{tb['num_matches']} ({wb*5}%)"
            d_str = f"{wd:+d} ({wd*5:+d}%)"
            s_diff = f"{sd:+7.1f} pts"
            print(f"{cname[:42]:<42} | {p:<22} | {w_str_a:<11} | {w_str_b:<11} | {d_str:<10} | {s_diff:<11}")

        comparison["series_comparisons"][t_key] = series_data
        print("-" * 120)

    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(comparison, f, indent=2)
    print(f"\nDetailed comparative metrics persisted to: {out_file}\n")

if __name__ == "__main__":
    run_comparison()

