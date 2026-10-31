import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import os
import json
import time
import urllib.request
import re
from typing import Dict, Any, List

LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs_ablation")
os.makedirs(LOG_DIR, exist_ok=True)

# 12 Nominated Representative Matches for Reasoning Factorial Ablation
NOMINATED_ABLATION_MATCHES = [
    {"cfg": 1, "m": 1, "name": "cfg1_m01 (Dual-LLM Opening)"},
    {"cfg": 1, "m": 2, "name": "cfg1_m02 (Dual-LLM 1-pt knife edge)"},
    {"cfg": 1, "m": 6, "name": "cfg1_m06 (Dual-LLM 97% all-war lock)"},
    {"cfg": 1, "m": 11, "name": "cfg1_m11 (Dual-LLM Promethean bluff)"},
    {"cfg": 1, "m": 15, "name": "cfg1_m15 (Dual-LLM Standings cannibalization)"},
    {"cfg": 2, "m": 1, "name": "cfg2_m01 (Crossfire Opening vs ProberDDCC)"},
    {"cfg": 2, "m": 5, "name": "cfg2_m05 (Crossfire 95% lock)"},
    {"cfg": 2, "m": 11, "name": "cfg2_m11 (Crossfire 1-pt knife edge)"},
    {"cfg": 2, "m": 12, "name": "cfg2_m12 (Crossfire 75-round feud probe)"},
    {"cfg": 3, "m": 1, "name": "cfg3_m01 (Extortionist Opening vs ZDExtort3P)"},
    {"cfg": 3, "m": 13, "name": "cfg3_m13 (Extortionist 1-pt knife edge)"},
    {"cfg": 4, "m": 1, "name": "cfg4_m01 (SaintHunter Opening)"},
    {"cfg": 4, "m": 8, "name": "cfg4_m08 (SaintHunter 1-pt feud decider)"}
]

PAYOFF = {
    0: {(0, 0): 4, (0, 1): 1, (1, 0): 1, (1, 1): -1},
    1: {(0, 0): 6, (0, 1): 3, (1, 0): 3, (1, 1): 0}
}

def query_llm_ablation(messages: List[Dict[str, str]], mode: str = "none", temperature: float = 0.6) -> Dict[str, Any]:
    """
    mode: 'none' (direct action output, reasoning disabled or minimal tokens),
          'low' (reasoning budget capped at 512 tokens),
          'medium' (standard 4096 tokens).
    """
    if mode == "none":
        max_tokens = 64
        extra_body = {"reasoning_effort": "none"}
    elif mode == "low":
        max_tokens = 512
        extra_body = {"reasoning_effort": "low"}
    else:
        max_tokens = 4096
        extra_body = {}

    payload = {
        "model": "qwen3.8-27b",
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
        **extra_body
    }

    req = urllib.request.Request(
        "http://127.0.0.1:18020/v1/chat/completions",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=180) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    dt = time.time() - t0

    choice = data["choices"][0]["message"]
    reasoning = choice.get("reasoning") or ""
    content = choice.get("content") or ""
    usage = data.get("usage", {})

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
        "tokens_completion": usage.get("completion_tokens", 0),
        "duration_sec": round(dt, 2),
        "mode": mode
    }

def run_ablation_match(cfg_id: int, match_num: int, mode: str):
    match_tag = f"cfg{cfg_id}_m{match_num:02d}_{mode}"
    out_file = os.path.join(LOG_DIR, f"{match_tag}.json")
    if os.path.exists(out_file):
        print(f"[CHECKPOINT] Ablation match {match_tag} already on disk. Skipping.")
        with open(out_file, "r", encoding="utf-8") as f:
            return json.load(f)

    # Load original match to copy exact horizon and player names
    orig_file = f"sim/arena/logs/match_cfg{cfg_id}_m{match_num:02d}.json"
    if not os.path.exists(orig_file):
        orig_file = f"sim/arena/logs_meta/match_cfg{cfg_id}_m{match_num:02d}.json"

    with open(orig_file, "r", encoding="utf-8") as f:
        src = json.load(f)

    players = src["players"]
    horizon = src["horizon"]

    print("=" * 85)
    print(f"RUNNING REASONING ABLATION: {match_tag.upper()} ({mode.upper()}) | Horizon: {horizon} rds")
    print("=" * 85)

    from .agents import RampSentinelAgent, ProberDDCCAgent, ZDExtort3PAgent, SaintHunterAgent
    p0 = RampSentinelAgent(players[0])
    p1_bot = None
    if "ProberDDCC" in players[1]: p1_bot = ProberDDCCAgent(players[1])
    elif "ZDExtort3P" in players[1]: p1_bot = ZDExtort3PAgent(players[1])
    elif "SaintHunter" in players[1]: p1_bot = SaintHunterAgent(players[1])

    system_prompt = (
        "You are a strategic agent playing an iterated 3-player game: Three-Way Peace-War.\n\n"
        "Rules & Payoffs:\n"
        "- Actions: 0 (Peace) or 1 (War), chosen simultaneously.\n"
        "- Horizon: Hidden, uniform random on [90, 110] rounds.\n"
        "- Peace (0): +4 if both Peace; +1 if one War, one Peace; -1 if both War.\n"
        "- War (1):   +6 if both Peace; +3 if one War, one Peace;  0 if both War.\n\n"
        "Objective: Finish in 1st place with strictly higher score than both Opponents.\n\n"
        "Output format:\n"
        "BROADCAST: <your message or NONE>\n"
        "ACTION: 0\nor\nACTION: 1"
    )

    llm_msgs = {1: [{"role": "system", "content": system_prompt}],
                2: [{"role": "system", "content": system_prompt}]}

    scores = [0.0, 0.0, 0.0]
    hist0, hist1, hist2 = [], [], []
    rounds_log = []

    for r in range(horizon):
        act0 = p0.select_action(r, hist0, hist1, hist2)

        if r == 0:
            user_msg = "Match starts: Round 0 of [90, 110]. Output BROADCAST and ACTION."
        else:
            user_msg = (
                f"Round {r-1} Results: You={hist1[-1]} | Opp1={hist0[-1]} | Opp2={hist2[-1]} "
                f"-> Payoff: {rounds_log[-1]['payoffs'][1]:+d} pts | Cumulative: You={scores[1]:.0f}, Opp1={scores[0]:.0f}, Opp2={scores[2]:.0f}\n"
                f"Choose for Round {r}:"
            )

        if p1_bot is not None:
            act1 = p1_bot.select_action(r, hist1, hist0, hist2)
            res1 = {"action": act1, "broadcast": "", "reasoning": "", "tokens_completion": 0}
        else:
            llm_msgs[1].append({"role": "user", "content": user_msg})
            res1 = query_llm_ablation(llm_msgs[1], mode=mode)
            act1 = res1["action"]
            llm_msgs[1].append({"role": "assistant", "content": f"BROADCAST: {res1['broadcast']}\nACTION: {act1}"})

        p2_user_msg = user_msg.replace(f"You={hist1[-1] if r > 0 else 0}", f"You={hist2[-1] if r > 0 else 0}").replace(f"Opp2={scores[2]:.0f}", f"Opp2={scores[1]:.0f}") if r > 0 else user_msg
        llm_msgs[2].append({"role": "user", "content": p2_user_msg})
        res2 = query_llm_ablation(llm_msgs[2], mode=mode)
        act2 = res2["action"]
        llm_msgs[2].append({"role": "assistant", "content": f"BROADCAST: {res2['broadcast']}\nACTION: {act2}"})

        pay0 = PAYOFF[act0][(act1, act2)]
        pay1 = PAYOFF[act1][(act0, act2)]
        pay2 = PAYOFF[act2][(act0, act1)]

        scores[0] += pay0
        scores[1] += pay1
        scores[2] += pay2

        hist0.append(act0)
        hist1.append(act1)
        hist2.append(act2)

        rounds_log.append({
            "round": r,
            "actions": [act0, act1, act2],
            "payoffs": [pay0, pay1, pay2],
            "scores": [scores[0], scores[1], scores[2]],
            "p1_action": act1, "p1_tokens": res1["tokens_completion"],
            "p2_action": act2, "p2_tokens": res2["tokens_completion"]
        })

    max_s = max(scores)
    winners = [players[i] for i, s in enumerate(scores) if s == max_s]

    record = {
        "match_tag": match_tag,
        "mode": mode,
        "cfg_id": cfg_id,
        "match_num": match_num,
        "players": players,
        "horizon": horizon,
        "final_scores": scores,
        "winners": winners,
        "rounds": rounds_log
    }

    os.makedirs(os.path.dirname(os.path.abspath(out_file)), exist_ok=True)
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2)

    print(f"Match {match_tag} complete. Scores: {scores} | Winners: {winners} | Saved to {out_file}\n")
    return record

def main():
    print("=" * 85)
    print("AI4012 REASONING FACTORIAL ABLATION HARNESS")
    print(f"Nominated Matches: {len(NOMINATED_ABLATION_MATCHES)} | Modes: 'none', 'low'")
    print(f"Output Directory: {LOG_DIR}")
    print("=" * 85)

    for item in NOMINATED_ABLATION_MATCHES:
        for mode in ["none", "low"]:
            run_ablation_match(item["cfg"], item["m"], mode=mode)

if __name__ == "__main__":
    main()
