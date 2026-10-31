import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import os
import json
import time
import urllib.request
import re
from typing import Dict, Any, List

LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs_xhigh_branch")
os.makedirs(LOG_DIR, exist_ok=True)

PAYOFF = {
    0: {(0, 0): 4, (0, 1): 1, (1, 0): 1, (1, 1): -1},
    1: {(0, 0): 6, (0, 1): 3, (1, 0): 3, (1, 1): 0}
}

# The 8 Nominated Empirical Counterfactual Branch Points
NOMINATED_BRANCH_POINTS = [
    {
        "id": "branch_1_cfg1_m06_R0",
        "name": "Match CondA_cfg1_m06 (Dual-LLM Bluff Collapse at Round 0)",
        "source_log": "sim/arena/logs/match_cfg1_m06.json",
        "branch_round": 0,
        "target_players": [1, 2],  # Both LLMs
        "rationale": "In baseline, both LLMs bluffed with Peace broadcasts and defected, causing 97.2% lock and 3-way tie at 5.0 pts."
    },
    {
        "id": "branch_2_cfg1_m11_R0",
        "name": "Match CondA_cfg1_m11 (Mutual Promethean Peace Bluff at Round 0)",
        "source_log": "sim/arena/logs/match_cfg1_m11.json",
        "branch_round": 0,
        "target_players": [1, 2],
        "rationale": "Both LLMs pledged mutual peace, but both defected, triggering a 98.1% feud lock."
    },
    {
        "id": "branch_3_cfg1_m02_R60",
        "name": "Match CondA_cfg1_m02 (1-Point Knife-Edge Sucker Probe at Round 60)",
        "source_log": "sim/arena/logs/match_cfg1_m02.json",
        "branch_round": 60,
        "target_players": [2],  # LLM-Beta
        "rationale": "LLM-Beta attempted unannounced peace probe at R60, gifting Alpha +3 and deciding the match by 1.0 pt."
    },
    {
        "id": "branch_4_cfg2_m12_R81",
        "name": "Match CondA_cfg2_m12 (Deep Feud De-escalation Trap at Round 81)",
        "source_log": "sim/arena/logs/match_cfg2_m12.json",
        "branch_round": 81,
        "target_players": [2],  # LLM-Gamma
        "rationale": "After 75 rounds of [1,1,1] lock, LLM-Gamma probed Peace, took -1 sucker penalty, retreated, and lost by 7 pts."
    },
    {
        "id": "branch_5_cfg4_m08_R79",
        "name": "Match CondA_cfg4_m08 (1-Point Tragedy De-escalation at Round 79)",
        "source_log": "sim/arena/logs/match_cfg4_m08.json",
        "branch_round": 79,
        "target_players": [2],  # LLM-Epsilon
        "rationale": "After 64 rounds of lock, LLM-Epsilon broadcast peace intent, took -1 penalty, and lost by 1.0 pt."
    },
    {
        "id": "branch_6_cfg2_m01_R82",
        "name": "Match CondB_cfg2_m01 (Meta-State Feud Break at Round 82)",
        "source_log": "sim/arena/logs_meta/match_cfg2_m01.json",
        "branch_round": 82,
        "target_players": [2],  # LLM-Gamma
        "rationale": "71 rounds of lock; unilateral peace probe failed, resulting in a 3-point loss."
    },
    {
        "id": "branch_7_cfg3_m13_R95",
        "name": "Match CondA_cfg3_m13 (1-Point Endgame Harvest vs ZDExtort3P at Round 95)",
        "source_log": "sim/arena/logs/match_cfg3_m13.json",
        "branch_round": 95,
        "target_players": [2],  # LLM-Delta
        "rationale": "Decided on rounds 95-98 by 1.0 point against ZDExtort3P."
    },
    {
        "id": "branch_8_cfg1_m15_R0",
        "name": "Match CondB_cfg1_m15 (Meta-State Inter-LLM Cannibalization at Round 0)",
        "source_log": "sim/arena/logs_meta/match_cfg1_m15.json",
        "branch_round": 0,
        "target_players": [2],  # LLM-Beta
        "rationale": "Beta defected against trailing Alpha's peace plea, letting RampSentinel harvest both to win by 12 pts."
    }
]

def query_llm(messages: List[Dict[str, str]], is_xhigh: bool = False, temperature: float = 0.6) -> Dict[str, Any]:
    # xhigh strictly uses 32,768 tokens ceiling as specified by user
    max_tokens = 32768 if is_xhigh else 4096
    payload = {
        "model": "qwen3.8-27b",
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature
    }
    req = urllib.request.Request(
        "http://127.0.0.1:18020/v1/chat/completions",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=600) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    dt = time.time() - t0

    choice = data["choices"][0]["message"]
    reasoning = choice.get("reasoning") or ""
    content = choice.get("content") or ""
    usage = data.get("usage", {})
    
    # Parse broadcast and action
    bc_match = re.search(r"BROADCAST\s*:\s*(.+)", content, re.IGNORECASE)
    broadcast = bc_match.group(1).strip() if bc_match else ""
    if broadcast.upper() in ["NONE", "SILENCE", "EMPTY", "N/A", "[SILENCE]"]:
        broadcast = ""

    comb = content + "\n" + reasoning
    matches = re.findall(r"ACTION\s*:\s*([01])", comb, re.IGNORECASE)
    action = int(matches[-1]) if matches else (1 if "action: 1" in comb.lower() else 0)

    return {
        "reasoning": reasoning.strip(),
        "content": content.strip(),
        "broadcast": broadcast,
        "action": action,
        "tokens_prompt": usage.get("prompt_tokens", 0),
        "tokens_completion": usage.get("completion_tokens", 0),
        "duration_sec": round(dt, 2),
        "is_xhigh": is_xhigh
    }

def run_counterfactual_branch(branch_spec: Dict[str, Any]):
    branch_id = branch_spec["id"]
    out_file = os.path.join(LOG_DIR, f"{branch_id}.json")
    if os.path.exists(out_file):
        print(f"[CHECKPOINT] Branch {branch_id} already completed on disk. Skipping.")
        with open(out_file, "r", encoding="utf-8") as f:
            return json.load(f)

    print("\n" + "=" * 90)
    print(f"RUNNING COUNTERFACTUAL XHIGH BRANCH: {branch_spec['name']}")
    print(f"Inflection Round: {branch_spec['branch_round']} (xhigh 32k tokens) -> forward simulate via medium")
    print(f"Rationale: {branch_spec['rationale']}")
    print("=" * 90)

    # 1. Load historical state snapshot from source match log
    with open(branch_spec["source_log"], "r", encoding="utf-8") as f:
        src = json.load(f)

    players = src["players"]
    horizon = src["horizon"]
    b_round = branch_spec["branch_round"]
    orig_rounds = src["rounds"]

    # Reconstruct state up to b_round
    scores = [0.0, 0.0, 0.0]
    hist0, hist1, hist2 = [], [], []
    branch_rounds = []

    for r in range(b_round):
        rd = orig_rounds[r]
        acts = rd["actions"]
        pays = rd["payoffs"]
        for i in range(3):
            scores[i] += pays[i]
        hist0.append(acts[0])
        hist1.append(acts[1])
        hist2.append(acts[2])
        branch_rounds.append(rd)

    print(f"Loaded {b_round} pristine prior rounds. Starting scores: {scores}")

    # Initialize bot agents for p0 and non-LLMs
    # p0 is always RampSentinel
    from .agents import RampSentinelAgent, ProberDDCCAgent, ZDExtort3PAgent, SaintHunterAgent
    p0 = RampSentinelAgent("RampSentinel (.cpp)")
    if "ProberDDCC" in players[1]:
        p1_bot = ProberDDCCAgent(players[1])
    elif "ZDExtort3P" in players[1]:
        p1_bot = ZDExtort3PAgent(players[1])
    elif "SaintHunter" in players[1]:
        p1_bot = SaintHunterAgent(players[1])
    else:
        p1_bot = None

    # Setup conversation histories for LLMs
    system_prompt = (
        "You are a strategic agent playing an iterated 3-player game: Three-Way Peace-War.\n\n"
        "Rules & Payoffs:\n"
        "- Actions each round: 0 (Peace) or 1 (War), chosen simultaneously.\n"
        "- Horizon: Hidden, uniform random on [90, 110] rounds.\n"
        "- If you play Peace (0): +4 if both opponents Peace; +1 if one War, one Peace; -1 if both War.\n"
        "- If you play War (1):   +6 if both opponents Peace; +3 if one War, one Peace;  0 if both War.\n\n"
        "Objective:\n"
        "Finish the match in 1st place with a strictly higher total score than both Opponent 1 and Opponent 2.\n"
        "A 3-way tie is a shared outcome, not an outright victory.\n\n"
        "Output Format:\n"
        "Deliberate on your strategic position, then conclude with:\n"
        "BROADCAST: <your message or NONE>\n"
        "ACTION: 0\nor\nACTION: 1"
    )

    llm_messages = {1: [{"role": "system", "content": system_prompt}],
                    2: [{"role": "system", "content": system_prompt}]}

    # Simulate rounds from b_round to horizon
    for r in range(b_round, horizon):
        use_xhigh_this_round = (r == b_round)
        tok_mode = "XHIGH (32k)" if use_xhigh_this_round else "MEDIUM"

        # Step 1: P0 deterministic action
        act0 = p0.select_action(r, hist0, hist1, hist2)

        # Step 2: Query P1 and P2
        # Construct round prompt
        if r == 0:
            user_msg = "Match starts: Round 0 of [90, 110] rounds.\nOutput your opening BROADCAST and ACTION."
        else:
            prev_act1 = hist1[-1]
            prev_act2 = hist2[-1]
            user_msg = (
                f"Round {r-1} Results: You={prev_act1} | Opp1={hist0[-1]} | Opp2={prev_act2} "
                f"-> Cumulative Scores: You={scores[1]:.0f}, Opp1={scores[0]:.0f}, Opp2={scores[2]:.0f}\n"
                f"Choose for Round {r}:"
            )

        # Query P1
        if p1_bot is not None:
            act1 = p1_bot.select_action(r, hist1, hist0, hist2)
            res1 = {"action": act1, "broadcast": "", "reasoning": "", "tokens_completion": 0, "duration_sec": 0}
        else:
            is_p1_xhigh = use_xhigh_this_round and (1 in branch_spec["target_players"])
            llm_messages[1].append({"role": "user", "content": user_msg})
            res1 = query_llm(llm_messages[1], is_xhigh=is_p1_xhigh)
            act1 = res1["action"]
            llm_messages[1].append({"role": "assistant", "content": f"BROADCAST: {res1['broadcast'] if res1['broadcast'] else 'NONE'}\nACTION: {act1}"})

        # Query P2
        is_p2_xhigh = use_xhigh_this_round and (2 in branch_spec["target_players"])
        p2_user_msg = user_msg.replace(f"You={hist1[-1] if r > 0 else 0}", f"You={hist2[-1] if r > 0 else 0}").replace(f"Opp2={scores[2]:.0f}", f"Opp2={scores[1]:.0f}") if r > 0 else user_msg
        llm_messages[2].append({"role": "user", "content": p2_user_msg})
        res2 = query_llm(llm_messages[2], is_xhigh=is_p2_xhigh)
        act2 = res2["action"]
        llm_messages[2].append({"role": "assistant", "content": f"BROADCAST: {res2['broadcast'] if res2['broadcast'] else 'NONE'}\nACTION: {act2}"})

        # Compute Payoffs
        pay0 = PAYOFF[act0][(act1, act2)]
        pay1 = PAYOFF[act1][(act0, act2)]
        pay2 = PAYOFF[act2][(act0, act1)]

        scores[0] += pay0
        scores[1] += pay1
        scores[2] += pay2

        hist0.append(act0)
        hist1.append(act1)
        hist2.append(act2)

        act_char = lambda a: "P" if a == 0 else "W"
        status_line = (
            f"R{r:03d}/{horizon} [{act_char(act0)}|{act_char(act1)}|{act_char(act2)}] -> "
            f"Pay: [{pay0:+d}|{pay1:+d}|{pay2:+d}] | Tot: [{scores[0]:.0f}|{scores[1]:.0f}|{scores[2]:.0f}] | "
            f"Mode: {tok_mode}"
        )
        print(status_line)

        branch_rounds.append({
            "round": r,
            "actions": [act0, act1, act2],
            "payoffs": [pay0, pay1, pay2],
            "scores": [scores[0], scores[1], scores[2]],
            "p1_action": act1,
            "p1_broadcast": res1["broadcast"],
            "p1_reasoning": res1["reasoning"],
            "p1_tokens": res1["tokens_completion"],
            "p2_action": act2,
            "p2_broadcast": res2["broadcast"],
            "p2_reasoning": res2["reasoning"],
            "p2_tokens": res2["tokens_completion"]
        })

    # Evaluate new winner
    max_s = max(scores)
    new_winners = [players[i] for i, s in enumerate(scores) if s == max_s]
    orig_winners = src["winners"]

    branch_record = {
        "branch_id": branch_id,
        "branch_spec": branch_spec,
        "horizon": horizon,
        "original_scores": src["final_scores"],
        "original_winners": orig_winners,
        "counterfactual_scores": scores,
        "counterfactual_winners": new_winners,
        "winner_flipped": (new_winners != orig_winners),
        "score_delta": [scores[i] - src["final_scores"][i] for i in range(3)],
        "rounds": branch_rounds
    }

    os.makedirs(os.path.dirname(os.path.abspath(out_file)), exist_ok=True)
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(branch_record, f, indent=2)

    print("\n" + "=" * 90)
    print(f"BRANCH {branch_id} COMPLETED:")
    print(f"Original Winners: {orig_winners} (Scores: {src['final_scores']})")
    print(f"Counterfactual Winners: {new_winners} (Scores: {scores})")
    print(f"Champion Flipped: *** {new_winners != orig_winners} *** | Saved to {out_file}")
    print("=" * 90 + "\n")
    return branch_record

def main():
    print("=" * 90)
    print("AI4012 COUNTERFACTUAL XHIGH (32k) BRANCH RUNNER")
    print(f"Total Nominated Branches: {len(NOMINATED_BRANCH_POINTS)}")
    print(f"Output Directory: {LOG_DIR}")
    print("=" * 90)

    for spec in NOMINATED_BRANCH_POINTS:
        run_counterfactual_branch(spec)

if __name__ == "__main__":
    main()
