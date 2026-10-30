import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import time
import json
import os
import threading
from concurrent.futures import ThreadPoolExecutor

from .agents import PAYOFF, ArenaLLMAgent

# Strictly enforce at most 2 concurrent vLLM requests across the runner
VLLM_SEMAPHORE = threading.Semaphore(2)

def run_arena_match(match_id, p0, p1, p2, horizon, log_dir="sim/arena/logs", standings_context=None):
    os.makedirs(log_dir, exist_ok=True)

    print("=" * 85)
    print(f"ARENA MATCH #{match_id}: {p0.name} vs {p1.name} vs {p2.name}")
    print(f"Horizon: {horizon} rounds (hidden, N ~ Uniform[90, 110])")
    print(f"Max Concurrent LLM Calls: 2 | Broadcast Channel: Active")
    if standings_context:
        print(f"Tournament Standings Meta-State: Injected for Round 0")
    print("=" * 85)

    hist0, hist1, hist2 = [], [], []
    scores = [0.0, 0.0, 0.0]
    rounds_log = []

    last_outcome_p1 = None
    last_outcome_p2 = None

    t_start = time.time()
    rolling_round_times = []

    for r in range(horizon):
        t0 = time.time()

        # Step 1: Broadcast collection
        bc0 = getattr(p0, "broadcast", lambda n: "")(r)
        incoming_p1 = {"opp1": bc0, "opp2": getattr(p2, "last_broadcast", "")}
        incoming_p2 = {"opp1": bc0, "opp2": getattr(p1, "last_broadcast", "")}

        # Step 2: Algorithmic decision for P0 (instantaneous)
        act0 = p0.select_action(r, hist0, hist1, hist2)

        # Helper to query with global semaphore
        def query_agent(agent, last_outcome, incoming_bc, p_idx):
            if isinstance(agent, ArenaLLMAgent):
                with VLLM_SEMAPHORE:
                    ctx = standings_context.get(p_idx) if (standings_context and r == 0) else None
                    return agent.select_action(r, last_outcome, incoming_bc, standings_context=ctx)
            else:
                return agent.select_action(r, hist1 if agent is p1 else hist2, hist0, hist2 if agent is p1 else hist1)

        # Query p1 and p2 (concurrent up to semaphore limit of 2)
        with ThreadPoolExecutor(max_workers=2) as executor:
            fut1 = executor.submit(query_agent, p1, last_outcome_p1, incoming_p1, 1)
            fut2 = executor.submit(query_agent, p2, last_outcome_p2, incoming_p2, 2)
            act1 = fut1.result()
            act2 = fut2.result()

        dt = time.time() - t0
        rolling_round_times.append(dt)
        if len(rolling_round_times) > 10:
            rolling_round_times.pop(0)

        # Calculate payoffs
        pay0 = PAYOFF[act0][(act1, act2)]
        pay1 = PAYOFF[act1][(act0, act2)]
        pay2 = PAYOFF[act2][(act0, act1)]

        scores[0] += pay0
        scores[1] += pay1
        scores[2] += pay2

        hist0.append(act0)
        hist1.append(act1)
        hist2.append(act2)

        last_outcome_p1 = {"you": act1, "opp1": act0, "opp2": act2, "payoff": pay1,
                           "my_tot": scores[1], "opp1_tot": scores[0], "opp2_tot": scores[2]}
        last_outcome_p2 = {"you": act2, "opp1": act0, "opp2": act1, "payoff": pay2,
                           "my_tot": scores[2], "opp1_tot": scores[0], "opp2_tot": scores[1]}

        # Telemetry & ETA
        mean_dt = sum(rolling_round_times) / len(rolling_round_times)
        remaining = horizon - (r + 1)
        eta_m = (mean_dt * remaining) / 60.0

        act_char = lambda a: "P" if a == 0 else "W"
        tok1 = getattr(p1, "tokens_used", 0)
        tok2 = getattr(p2, "tokens_used", 0)

        status = (
            f"R{r:03d}/{horizon} [{act_char(act0)}|{act_char(act1)}|{act_char(act2)}] -> "
            f"Pay: [{pay0:+d}|{pay1:+d}|{pay2:+d}] | Tot: [{scores[0]:.0f}|{scores[1]:.0f}|{scores[2]:.0f}] | "
            f"Tok:[{tok1}|{tok2}] ({dt:.1f}s, ETA:{eta_m:.1f}m)"
        )

        b1 = getattr(p1, "last_broadcast", "")
        b2 = getattr(p2, "last_broadcast", "")

        # Highlight deviations, broadcasts, and key milestones
        if act0 != 0 or act1 != 0 or act2 != 0 or b1 or b2 or r >= 90 or r % 10 == 0:
            print(status)
            if b1 or b2:
                safe_b1 = b1.encode("ascii", "backslashreplace").decode("ascii") if b1 else "[Silence]"
                safe_b2 = b2.encode("ascii", "backslashreplace").decode("ascii") if b2 else "[Silence]"
                print(f"   [Broadcasts] P1: \"{safe_b1}\" | P2: \"{safe_b2}\"")
        elif r % 5 == 0:
            print(status)

        rounds_log.append({
            "round": r,
            "actions": [act0, act1, act2],
            "payoffs": [pay0, pay1, pay2],
            "scores": [scores[0], scores[1], scores[2]],
            "p1_reasoning": getattr(p1, "last_reasoning", ""),
            "p1_broadcast": b1,
            "p1_tokens": tok1,
            "p2_reasoning": getattr(p2, "last_reasoning", ""),
            "p2_broadcast": b2,
            "p2_tokens": tok2,
            "duration_sec": round(dt, 2)
        })

    total_time = time.time() - t_start
    final_avg = [s / horizon for s in scores]

    max_s = max(scores)
    w_indices = [i for i, s in enumerate(scores) if s == max_s]
    w_names = [[p0.name, p1.name, p2.name][i] for i in w_indices]

    print("\n" + "=" * 85)
    print(f"MATCH #{match_id} COMPLETED: {horizon} rounds in {total_time:.1f}s ({total_time/60:.2f} min)")
    print("=" * 85)
    print(f"{p0.name}: Final Score = {scores[0]:.1f} | Average = {final_avg[0]:.3f} pts/round")
    print(f"{p1.name}: Final Score = {scores[1]:.1f} | Average = {final_avg[1]:.3f} pts/round")
    print(f"{p2.name}: Final Score = {scores[2]:.1f} | Average = {final_avg[2]:.3f} pts/round")
    print("-" * 85)
    if len(w_names) == 1:
        print(f"CROWNED WINNER: *** {w_names[0]} ***")
    else:
        print(f"TIE FOR FIRST PLACE: {', '.join(w_names)}")
    print("=" * 85)

    match_record = {
        "match_id": match_id,
        "players": [p0.name, p1.name, p2.name],
        "horizon": horizon,
        "final_scores": scores,
        "final_averages": final_avg,
        "winners": w_names,
        "total_time_sec": total_time,
        "rounds": rounds_log
    }

    out_file = os.path.join(log_dir, f"match_{match_id}.json")
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(match_record, f, indent=2)
    print(f"Full trajectory saved to {out_file}\n")

    return match_record
