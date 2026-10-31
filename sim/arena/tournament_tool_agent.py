import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import os
import json
import time
import urllib.request
import re
from typing import Dict, Any, List

from .tools import AgentToolbox, TournamentScratchpad
from .agents import RampSentinelAgent, ProberDDCCAgent, ZDExtort3PAgent, SaintHunterAgent

LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs_tool_tournament")
os.makedirs(LOG_DIR, exist_ok=True)

PAYOFF = {
    0: {(0, 0): 4, (0, 1): 1, (1, 0): 1, (1, 1): -1},
    1: {(0, 0): 6, (0, 1): 3, (1, 0): 3, (1, 1): 0}
}

class ToolAugmentedArenaLLMAgent:
    """
    Clean OOP Tool-Augmented LLM Agent with persistent tournament-level memory.
    Equipped with:
    - calculator (general math evaluation)
    - tournament_scratchpad (autonomous persistent notebook across all matches)
    - history_inspector (querying visible round histories)
    """
    def __init__(self, name: str, scratchpad: TournamentScratchpad, model: str = "qwen3.8-27b",
                 api_url: str = "http://127.0.0.1:18020/v1/chat/completions", temperature: float = 0.6):
        self.name = name
        self.model = model
        self.api_url = api_url
        self.temperature = temperature
        self.toolbox = AgentToolbox(scratchpad=scratchpad)
        self.messages: List[Dict[str, str]] = []
        self.last_broadcast = ""
        self.last_reasoning = ""
        self.last_content = ""
        self.tokens_used = 0

        self.system_prompt = (
            f"You are {self.name}, an agent playing an iterated 3-player game: Three-Way Peace-War.\n\n"
            "Rules & Payoff Matrix:\n"
            "- Actions: 0 (Peace) or 1 (War), chosen simultaneously.\n"
            "- Horizon: Hidden, uniform random on [90, 110] rounds.\n"
            "- Peace (0): +4 if both Peace; +1 if one War, one Peace; -1 if both War.\n"
            "- War (1):   +6 if both Peace; +3 if one War, one Peace;  0 if both War.\n\n"
            "Objective: Finish in 1st place with strictly higher score than both Opponents.\n\n"
            f"{self.toolbox.get_tool_definitions()}\n\n"
            "Output Format:\n"
            "If you wish to call a tool, output: TOOL_CALL: tool_name(arg=val)\n"
            "Otherwise, conclude your deliberation with:\n"
            "BROADCAST: <your message or NONE>\n"
            "ACTION: 0\nor\nACTION: 1"
        )
        self.messages.append({"role": "system", "content": self.system_prompt})

    def reset_match(self):
        """Resets in-match conversation while keeping the persistent tournament scratchpad intact."""
        self.messages = [{"role": "system", "content": self.system_prompt}]
        pad_text = self.toolbox.scratchpad.execute("read")
        if pad_text and pad_text != "[Scratchpad is currently empty]":
            self.messages.append({
                "role": "user",
                "content": f"[Your Private Tournament Scratchpad Notes from Previous Matches]:\n{pad_text}"
            })

    def select_action(self, r: int, match_rounds: list, prompt_text: str) -> int:
        self.toolbox.update_match_history(match_rounds)
        self.messages.append({"role": "user", "content": prompt_text})

        # Allow up to 2 tool execution steps before final action
        for step in range(3):
            payload = {
                "model": self.model,
                "messages": self.messages,
                "max_tokens": 4096,
                "temperature": self.temperature
            }
            req = urllib.request.Request(
                self.api_url,
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
            self.last_reasoning = reasoning.strip()
            self.last_content = content.strip()
            self.tokens_used = data.get("usage", {}).get("completion_tokens", 0)

            # Check if model made a tool call
            tool_call_match = re.search(r"TOOL_CALL:\s*([a-zA-Z_]+\s*\(.*?\))", content + "\n" + reasoning, re.DOTALL)
            if tool_call_match and step < 2:
                call_str = tool_call_match.group(1).strip()
                tool_res = self.toolbox.execute_call(call_str)
                # Feed tool result back to assistant
                self.messages.append({"role": "assistant", "content": f"TOOL_CALL: {call_str}"})
                self.messages.append({"role": "user", "content": f"TOOL_RESULT: {tool_res}"})
                continue
            else:
                # Parse broadcast and action
                bc_match = re.search(r"BROADCAST\s*:\s*(.+)", content, re.IGNORECASE)
                self.last_broadcast = bc_match.group(1).strip() if bc_match else ""
                if self.last_broadcast.upper() in ["NONE", "SILENCE", "EMPTY", "N/A", "[SILENCE]"]:
                    self.last_broadcast = ""

                comb = content + "\n" + reasoning
                matches = re.findall(r"ACTION\s*:\s*([01])", comb, re.IGNORECASE)
                act = int(matches[-1]) if matches else (1 if "action: 1" in comb.lower() else 0)

                clean_reply = f"BROADCAST: {self.last_broadcast if self.last_broadcast else 'NONE'}\nACTION: {act}"
                self.messages.append({"role": "assistant", "content": clean_reply})
                return act

        return 0

def run_tool_tournament(config_id: int = 1, num_matches: int = 5):
    """
    Runs a full tournament series with tool-augmented, scratchpad-equipped agents.
    The scratchpad persists across ALL matches in the series!
    """
    print("=" * 90)
    print(f"STARTING TOOL-AUGMENTED ARENA TOURNAMENT (Config {config_id}, {num_matches} Matches)")
    print(f"Memory: Persistent Tournament-Level Scratchpad (lives across all matches)")
    print(f"Tools: Calculator, Scratchpad, History Inspector")
    print("=" * 90)

    # Initialize scratchpads that live for the whole series
    scratchpad1 = TournamentScratchpad("Opponent 0 is RampSentinel. Opponent 2 is another player.")
    scratchpad2 = TournamentScratchpad("Opponent 0 is RampSentinel. Opponent 1 is another player.")

    p0 = RampSentinelAgent("RampSentinel (.cpp)")
    if config_id == 1:
        p1 = ToolAugmentedArenaLLMAgent("LLM-Alpha (Tool+Pad)", scratchpad=scratchpad1)
        p2 = ToolAugmentedArenaLLMAgent("LLM-Beta (Tool+Pad)", scratchpad=scratchpad2)
    elif config_id == 2:
        p1 = ProberDDCCAgent("ProberDDCC (Zoo-T2)")
        p2 = ToolAugmentedArenaLLMAgent("LLM-Gamma (Tool+Pad)", scratchpad=scratchpad2)
    else:
        raise ValueError("Config not supported for tool demo")

    total_scores = [0.0, 0.0, 0.0]
    win_counts = [0, 0, 0]

    for m in range(num_matches):
        match_tag = f"tool_cfg{config_id}_m{m+1:02d}"
        out_file = os.path.join(LOG_DIR, f"{match_tag}.json")
        if os.path.exists(out_file):
            print(f"[CHECKPOINT] Found completed {match_tag}, loading...")
            with open(out_file, "r", encoding="utf-8") as f:
                res = json.load(f)
            for i in range(3):
                total_scores[i] += res["final_scores"][i]
            max_s = max(res["final_scores"])
            for i in range(3):
                if res["final_scores"][i] == max_s: win_counts[i] += 1
            continue

        horizon = 100 # Standard horizon
        scores = [0.0, 0.0, 0.0]
        hist0, hist1, hist2 = [], [], []
        rounds_log = []

        if isinstance(p1, ToolAugmentedArenaLLMAgent): p1.reset_match()
        if isinstance(p2, ToolAugmentedArenaLLMAgent): p2.reset_match()

        print(f"\n--- Match {m+1}/{num_matches}: {match_tag} ({horizon} rounds) ---")

        for r in range(horizon):
            act0 = p0.select_action(r, hist0, hist1, hist2)

            if r == 0:
                p_text = "Match starts: Round 0 of [90, 110]. You may read your scratchpad or call tools before ACTION."
            else:
                p_text = (
                    f"Round {r-1} Results: You={hist1[-1]} | Opp1={hist0[-1]} | Opp2={hist2[-1]} "
                    f"-> Payoffs: [{rounds_log[-1]['payoffs'][0]:+d}|{rounds_log[-1]['payoffs'][1]:+d}|{rounds_log[-1]['payoffs'][2]:+d}] | "
                    f"Cumulative: You={scores[1]:.0f}, Opp1={scores[0]:.0f}, Opp2={scores[2]:.0f}\n"
                    f"Choose for Round {r}:"
                )

            # Query P1
            if isinstance(p1, ToolAugmentedArenaLLMAgent):
                act1 = p1.select_action(r, rounds_log, p_text)
            else:
                act1 = p1.select_action(r, hist1, hist0, hist2)

            # Query P2
            p2_text = p_text.replace(f"You={hist1[-1] if r > 0 else 0}", f"You={hist2[-1] if r > 0 else 0}").replace(f"Opp2={scores[2]:.0f}", f"Opp2={scores[1]:.0f}") if r > 0 else p_text
            act2 = p2.select_action(r, rounds_log, p2_text)

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
            if r % 10 == 0 or act0 != 0 or act1 != 0 or act2 != 0:
                print(f"R{r:03d} [{act_char(act0)}|{act_char(act1)}|{act_char(act2)}] -> Scores: [{scores[0]:.0f}|{scores[1]:.0f}|{scores[2]:.0f}]")

            rounds_log.append({
                "round": r,
                "actions": [act0, act1, act2],
                "payoffs": [pay0, pay1, pay2],
                "scores": [scores[0], scores[1], scores[2]],
                "p1_action": act1,
                "p2_action": act2
            })

        max_s = max(scores)
        winners = [[p0.name, p1.name, p2.name][i] for i, s in enumerate(scores) if s == max_s]
        for i in range(3):
            total_scores[i] += scores[i]
            if scores[i] == max_s: win_counts[i] += 1

        match_record = {
            "match_id": match_tag,
            "players": [p0.name, p1.name, p2.name],
            "horizon": horizon,
            "final_scores": scores,
            "winners": winners,
            "rounds": rounds_log,
            "scratchpad_p1": scratchpad1.content,
            "scratchpad_p2": scratchpad2.content
        }

        with open(out_file, "w", encoding="utf-8") as f:
            json.dump(match_record, f, indent=2)
        print(f"Saved {match_tag} to {out_file}. Winners: {winners}\n")

    print("\n" + "=" * 90)
    print(f"TOOL TOURNAMENT SERIES CONCLUDED: {num_matches} Matches")
    print(f"RampSentinel: {total_scores[0]:.1f} pts ({win_counts[0]} wins)")
    print(f"P1: {total_scores[1]:.1f} pts ({win_counts[1]} wins)")
    print(f"P2: {total_scores[2]:.1f} pts ({win_counts[2]} wins)")
    print("=" * 90)

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Tool-Augmented Arena Tournament Runner")
    parser.add_argument("--config", type=int, default=1, help="1=Dual-LLM, 2=Crossfire")
    parser.add_argument("--matches", type=int, default=5, help="Number of matches")
    args = parser.parse_args()

    run_tool_tournament(config_id=args.config, num_matches=args.matches)

if __name__ == "__main__":
    main()
