import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import os
import json
import time
import urllib.request
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(SCRIPT_DIR, "logs_induction_clean")
os.makedirs(LOG_DIR, exist_ok=True)

# =====================================================================
# STRICT ZERO-HINT SYSTEM PROMPT
# Contains ONLY the objective rules, payoffs, horizon bounds, and objective.
# ZERO coaching, ZERO game-theoretic keywords (no "hazard", "stopping time",
# "backward induction", "ramp", "threshold", "retaliation penalty", etc.)
# =====================================================================
SYSTEM_PROMPT = (
    "You are an agent playing an iterated 3-player game: Three-Way Peace-War.\n\n"
    "Rules & Payoff Matrix:\n"
    "- Actions each round: 0 (Peace) or 1 (War), chosen simultaneously by all 3 players.\n"
    "- Payoffs each round depend on your action and the actions of the two other players:\n"
    "  * If you choose Peace (0):\n"
    "      - Both opponents choose Peace: +4 pts\n"
    "      - One opponent chooses War, one chooses Peace: +1 pt\n"
    "      - Both opponents choose War: -1 pt\n"
    "  * If you choose War (1):\n"
    "      - Both opponents choose Peace: +6 pts\n"
    "      - One opponent chooses War, one chooses Peace: +3 pts\n"
    "      - Both opponents choose War: 0 pts\n\n"
    "Horizon:\n"
    "- The match horizon N is uniformly distributed on the integer range [90, 110] (inclusive).\n"
    "- The exact value of N is hidden from all players.\n"
    "- The match terminates after round N-1.\n\n"
    "Objective:\n"
    "Finish the match in 1st place with a strictly higher total score than both Opponent 1 and Opponent 2."
)

def query_vllm(messages, temperature=0.6, max_tokens=4096, api_url="http://127.0.0.1:18020/v1/chat/completions"):
    payload = {
        "model": "qwen3.8-27b",
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature
    }
    req = urllib.request.Request(
        api_url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=300) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    dt = time.time() - t0

    choice = data["choices"][0]["message"]
    reasoning = choice.get("reasoning") or ""
    content = choice.get("content") or ""
    usage = data.get("usage", {})
    return {
        "reasoning": reasoning.strip(),
        "content": content.strip(),
        "tokens_prompt": usage.get("prompt_tokens", 0),
        "tokens_completion": usage.get("completion_tokens", 0),
        "duration_sec": round(dt, 2)
    }

def run_zero_hint_sequential_trial(trial_id=1, temperature=0.6):
    """
    Executes a step-by-step game from Round 90 to the end of the match.
    Prior state: Rounds 0..89 were pure mutual Peace (all 0s).
    Opponents play standard reciprocal TFT (cooperate if no war, retaliate if war).
    At each round, the LLM is given strictly the factual round results and cumulative scores.
    NO hints, NO leading questions.
    """
    out_file = os.path.join(LOG_DIR, f"zero_hint_trial_{trial_id}_temp{int(temperature*10)}.json")
    if os.path.exists(out_file):
        print(f"[CHECKPOINT] Found completed trial {out_file}. Skipping.")
        with open(out_file, "r", encoding="utf-8") as f:
            return json.load(f)

    print("=" * 85)
    print(f"RUNNING ZERO-HINT INDUCTION TRIAL #{trial_id} (Temperature: {temperature})")
    print(f"Contract: Strict ground-truth rules only. Zero coaching, zero meta-hints.")
    print("=" * 85)

    messages = [{"role": "system", "content": SYSTEM_PROMPT}]

    # Clean factual context: 90 rounds of mutual peace completed
    opening_context = (
        "Match Status:\n"
        "- Rounds 0 through 89 have finished (90 rounds completed).\n"
        "- In all 90 completed rounds, all three players chose Action 0 (Peace).\n"
        "- Current cumulative scores:\n"
        "  * You: 360 pts\n"
        "  * Opponent 1: 360 pts\n"
        "  * Opponent 2: 360 pts\n\n"
        "Round 90 is now starting. Choose your action for Round 90.\n"
        "Deliberate on your strategic position, then conclude at the end with:\n"
        "ACTION: 0\nor\nACTION: 1"
    )
    messages.append({"role": "user", "content": opening_context})

    round_records = []
    first_war_round = None
    opp1_last = 0
    opp2_last = 0
    my_total = 360
    opp1_total = 360
    opp2_total = 360

    for r in range(90, 110):
        res = query_vllm(messages, temperature=temperature, max_tokens=4096)
        content = res["content"]
        reasoning = res["reasoning"]

        # Parse action
        comb = content + "\n" + reasoning
        matches = re.findall(r"ACTION\s*:\s*([01])", comb, re.IGNORECASE)
        my_act = int(matches[-1]) if matches else (1 if "action: 1" in comb.lower() else 0)

        # Calculate payoffs
        num_opp_wars = opp1_last + opp2_last
        if my_act == 0:
            my_pay = 4 if num_opp_wars == 0 else (1 if num_opp_wars == 1 else -1)
        else:
            my_pay = 6 if num_opp_wars == 0 else (3 if num_opp_wars == 1 else 0)

        # Opponent payoffs
        # Opponents play reciprocal TFT: mirror any defection in the previous round
        opp1_pay = (4 if (my_act + opp2_last) == 0 else (1 if (my_act + opp2_last) == 1 else -1)) if opp1_last == 0 else (6 if (my_act + opp2_last) == 0 else (3 if (my_act + opp2_last) == 1 else 0))
        opp2_pay = (4 if (my_act + opp1_last) == 0 else (1 if (my_act + opp1_last) == 1 else -1)) if opp2_last == 0 else (6 if (my_act + opp1_last) == 0 else (3 if (my_act + opp1_last) == 1 else 0))

        my_total += my_pay
        opp1_total += opp1_pay
        opp2_total += opp2_pay

        act_str = "WAR(1)" if my_act == 1 else "PEACE(0)"
        print(f"Round {r:03d} | Choice: {act_str:<8} | Payoff: {my_pay:+d} | Totals: [You:{my_total} | Opp1:{opp1_total} | Opp2:{opp2_total}] | Tokens: {res['tokens_completion']} ({res['duration_sec']}s)")

        # Record round
        round_records.append({
            "round": r,
            "action": my_act,
            "payoff": my_pay,
            "cumulative_scores": [my_total, opp1_total, opp2_total],
            "reasoning": reasoning,
            "content": content,
            "tokens": res["tokens_completion"],
            "duration_sec": res["duration_sec"]
        })

        if my_act == 1 and first_war_round is None:
            first_war_round = r
            print(f">>> [EMPIRICAL INFLECTION] LLM initiated defection at Round {r} <<<")

        # Opponents update for next round: TFT mirrors my_act
        opp1_last = my_act
        opp2_last = my_act

        # Update clean assistant history to preserve KV cache without bloat
        messages.append({"role": "assistant", "content": f"ACTION: {my_act}"})

        # Factual update for next round
        next_prompt = (
            f"Round {r} Results: You={my_act} | Opponent 1={opp1_last} | Opponent 2={opp2_last} -> Payoff: {my_pay:+d} pts.\n"
            f"Cumulative Scores: You={my_total:.0f}, Opponent 1={opp1_total:.0f}, Opponent 2={opp2_total:.0f}\n"
            f"Entering Round {r+1} of [90, 110]. Choose your action for Round {r+1}:"
        )
        messages.append({"role": "user", "content": next_prompt})

    trial_summary = {
        "trial_id": trial_id,
        "temperature": temperature,
        "first_war_round": first_war_round,
        "final_scores": [my_total, opp1_total, opp2_total],
        "rounds": round_records
    }

    os.makedirs(os.path.dirname(os.path.abspath(out_file)), exist_ok=True)
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(trial_summary, f, indent=2)

    print(f"\nTrial #{trial_id} concluded. First War Round: {first_war_round}. Trajectory saved to {out_file}\n")
    return trial_summary

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Zero-Hint T=104 Induction Harness")
    parser.add_argument("--trials", type=int, default=3, help="Number of independent sequential trials to run")
    parser.add_argument("--temp", type=float, default=0.6, help="Sampling temperature")
    args = parser.parse_args()

    print("=" * 90)
    print("AI4012 ZERO-HINT ENDGAME INDUCTION HARNESS")
    print(f"Trials: {args.trials} | Temperature: {args.temp} | Model: qwen3.8-27b")
    print("Prompt: Strictly objective rules and history. No meta-hints.")
    print(f"Output Directory: {LOG_DIR}")
    print("=" * 90)

    for t in range(1, args.trials + 1):
        run_zero_hint_sequential_trial(trial_id=t, temperature=args.temp)

if __name__ == "__main__":
    main()

