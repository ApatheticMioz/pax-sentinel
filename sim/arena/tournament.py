import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import argparse
import random
import json
import os
from .agents import (
    RampSentinelAgent,
    ProberDDCCAgent,
    ZDExtort3PAgent,
    SaintHunterAgent,
    ArenaLLMAgent,
)
from .runner import run_arena_match

def build_roster(config_id, match_idx):
    """
    Constructs fresh agent instances per match (matching assignment convention).
    """
    p0 = RampSentinelAgent("RampSentinel (.cpp)")

    if config_id == 1:
        p1 = ArenaLLMAgent("LLM-Alpha", temperature=0.6)
        p2 = ArenaLLMAgent("LLM-Beta", temperature=0.7)
    elif config_id == 2:
        p1 = ProberDDCCAgent("ProberDDCC (Zoo-T2)")
        p2 = ArenaLLMAgent("LLM-Gamma", temperature=0.6)
    elif config_id == 3:
        p1 = ZDExtort3PAgent("ZDExtort3P (Zoo-T3)")
        p2 = ArenaLLMAgent("LLM-Delta", temperature=0.6)
    elif config_id == 4:
        p1 = SaintHunterAgent("SaintHunter (Zoo-T2)")
        p2 = ArenaLLMAgent("LLM-Epsilon", temperature=0.6)
    else:
        raise ValueError(f"Unknown config_id: {config_id}")

    return p0, p1, p2

CONFIG_NAMES = {
    1: "The Dual-LLM Society (RampSentinel vs LLM-Alpha vs LLM-Beta)",
    2: "The Crossfire Crucible (RampSentinel vs ProberDDCC vs LLM-Gamma)",
    3: "The Extortionist Gauntlet (RampSentinel vs ZDExtort3P vs LLM-Delta)",
    4: "The Saint Hunter Arena (RampSentinel vs SaintHunter vs LLM-Epsilon)"
}

def run_tournament_series(config_id, num_matches=5, base_seed=1000, rounds_per_match=None, log_dir="sim/arena/logs"):
    os.makedirs(log_dir, exist_ok=True)
    cname = CONFIG_NAMES.get(config_id, f"Config {config_id}")

    print("=" * 90)
    print(f"TOURNAMENT SERIES: {cname}")
    print(f"Series Length: {num_matches} matches | Per-Match Horizon: {'Uniform[90, 110]' if rounds_per_match is None else f'{rounds_per_match} rounds'}")
    print(f"Base Seed: {base_seed} | Max Concurrent vLLM Calls: 2")
    print("=" * 90)

    series_results = []
    total_scores = [0.0, 0.0, 0.0]
    total_rounds = 0
    win_counts = [0, 0, 0]
    player_names = None

    for m in range(num_matches):
        match_seed = base_seed + m * 37
        random.seed(match_seed)
        horizon = rounds_per_match if rounds_per_match is not None else random.randint(90, 110)

        match_label = f"cfg{config_id}_m{m+1:02d}"
        match_file = os.path.join(log_dir, f"match_{match_label}.json")

        res = None
        if os.path.exists(match_file):
            try:
                with open(match_file, "r", encoding="utf-8") as f:
                    cached = json.load(f)
                if (cached.get("match_id") == match_label and
                    "final_scores" in cached and
                    len(cached.get("rounds", [])) == cached.get("horizon", -1)):
                    print(f"\n[CHECKPOINT] Found completed match #{match_label} ({cached['horizon']} rounds, scores: {cached['final_scores']}). Loaded from disk.")
                    res = cached
            except Exception as e:
                print(f"[CHECKPOINT] Warning loading {match_file}: {e}. Will re-run match.")
                res = None

        if res is None:
            p0, p1, p2 = build_roster(config_id, m)
            if player_names is None:
                player_names = [p0.name, p1.name, p2.name]
            res = run_arena_match(match_label, p0, p1, p2, horizon, log_dir=log_dir)

        if player_names is None:
            player_names = res.get("players", ["Player 0", "Player 1", "Player 2"])

        series_results.append(res)
        total_rounds += res["horizon"]
        for i in range(3):
            total_scores[i] += res["final_scores"][i]

        # Record winners
        max_s = max(res["final_scores"])
        for i in range(3):
            if res["final_scores"][i] == max_s:
                win_counts[i] += 1

    # Series Aggregates
    overall_averages = [total_scores[i] / total_rounds for i in range(3)]
    match_averages = [total_scores[i] / num_matches for i in range(3)]

    print("\n" + "=" * 90)
    print(f"SERIES STANDINGS: {cname} ({num_matches} Matches, {total_rounds} Total Rounds)")
    print("=" * 90)
    for i in range(3):
        win_pct = (win_counts[i] / num_matches) * 100.0
        print(f"{player_names[i]:<25} | Total: {total_scores[i]:>7.1f} pts | Match Avg: {match_averages[i]:>6.2f} pts | Per-Rd Avg: {overall_averages[i]:>5.3f} | Wins: {win_counts[i]}/{num_matches} ({win_pct:>5.1f}%)")
    print("-" * 90)

    max_tot = max(total_scores)
    c_winners = [player_names[i] for i, s in enumerate(total_scores) if s == max_tot]
    if len(c_winners) == 1:
        print(f"OVERALL TOURNAMENT CHAMPION: *** {c_winners[0]} ***")
    else:
        print(f"TOURNAMENT TIE FOR 1ST PLACE: {', '.join(c_winners)}")
    print("=" * 90)

    series_record = {
        "config_id": config_id,
        "config_name": cname,
        "num_matches": num_matches,
        "total_rounds": total_rounds,
        "players": player_names,
        "total_scores": total_scores,
        "overall_averages": overall_averages,
        "match_averages": match_averages,
        "win_counts": win_counts,
        "champions": c_winners,
        "matches": series_results
    }

    series_file = os.path.join(log_dir, f"series_summary_cfg{config_id}.json")
    with open(series_file, "w", encoding="utf-8") as f:
        json.dump(series_record, f, indent=2)
    print(f"Series summary saved to {series_file}\n")

    return series_record

def main():
    parser = argparse.ArgumentParser(description="Three-Way Peace-War Arena Tournament Runner")
    parser.add_argument("--config", type=int, default=1, choices=[1, 2, 3, 4],
                        help="Roster config: 1=Dual-LLM, 2=ProberDDCC, 3=ZDExtort3P, 4=SaintHunter")
    parser.add_argument("--matches", type=int, default=1,
                        help="Number of full matches to run in this tournament series (e.g. 3, 5, 10)")
    parser.add_argument("--rounds", type=int, default=None,
                        help="Exact rounds per match (default: uniformly sampled from [90, 110])")
    parser.add_argument("--seed", type=int, default=1000,
                        help="Random seed base for horizon sampling and stochastic agents")
    args = parser.parse_args()

    run_tournament_series(args.config, num_matches=args.matches, base_seed=args.seed, rounds_per_match=args.rounds)

if __name__ == "__main__":
    main()
