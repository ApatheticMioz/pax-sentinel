import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import math
import json
import re
from typing import Dict, Any, Optional

# =====================================================================
# Object-Oriented Agentic Tool Suite for Game-Theoretic LLMs
# General-purpose, strictly fair capabilities (no hidden game state,
# no pre-computed game-theoretic solutions).
# =====================================================================

class Tool:
    """Base class for agentic tools."""
    name: str
    description: str

    def execute(self, **kwargs) -> str:
        raise NotImplementedError


class CalculatorTool(Tool):
    """
    General-purpose mathematical expression evaluator.
    Enables agents to compute probabilities, combinatorics, expected values,
    and arithmetic without mental computation errors.
    """
    name = "calculator"
    description = (
        "Evaluates a mathematical expression in Python syntax.\n"
        "Available functions: standard operators (+, -, *, /, **, %), math.comb, math.perm, "
        "math.exp, math.log, math.sqrt, sum, min, max, round, abs.\n"
        "Argument: expression (str)"
    )

    def execute(self, expression: str) -> str:
        safe_dict = {k: v for k, v in math.__dict__.items() if not k.startswith("__")}
        safe_dict.update({
            "abs": abs,
            "round": round,
            "min": min,
            "max": max,
            "sum": sum,
            "int": int,
            "float": float
        })
        try:
            # Clean expression
            expr = expression.strip()
            # Safety: reject dangerous builtins
            if any(forbidden in expr for forbidden in ["import", "exec", "eval", "open", "sys", "os", "__"]):
                return "Error: Forbidden token in expression."
            res = eval(expr, {"__builtins__": None}, safe_dict)
            return str(res)
        except Exception as e:
            return f"Calculation Error: {e}"


class TournamentScratchpad(Tool):
    """
    Persistent in-memory and on-disk scratchpad that lives for an ENTIRE tournament series.
    Enables autonomous agent opponent modeling, hypothesis tracking, and strategic memory.
    The agent has total agency to read, write, or append whatever notes it deems relevant.
    """
    name = "tournament_scratchpad"
    description = (
        "Persistent private strategic notebook that survives across ALL matches in the tournament series.\n"
        "Commands:\n"
        "- action='read': Returns your current private notes.\n"
        "- action='write', content='...': Overwrites your private notes.\n"
        "- action='append', content='...': Appends a new observation/note.\n"
        "- action='clear': Clears your scratchpad."
    )

    def __init__(self, initial_content: str = ""):
        self.content = initial_content

    def execute(self, action: str, content: str = "") -> str:
        action = action.lower().strip()
        if action == "read":
            return self.content if self.content else "[Scratchpad is currently empty]"
        elif action == "write":
            self.content = content.strip()
            return f"Scratchpad updated ({len(self.content)} characters)."
        elif action == "append":
            if self.content:
                self.content += "\n" + content.strip()
            else:
                self.content = content.strip()
            return f"Note appended to scratchpad. Total length: {len(self.content)} characters."
        elif action == "clear":
            self.content = ""
            return "Scratchpad cleared."
        else:
            return f"Unknown scratchpad action '{action}'. Valid actions: read, write, append, clear."


class HistoryInspectorTool(Tool):
    """
    Enables the agent to inspect subsets of the historical round log
    within the current match without hallucinating past round indices.
    """
    name = "history_inspector"
    description = (
        "Inspects visible match history.\n"
        "Commands:\n"
        "- action='recent', k=5: Returns the last k rounds of actions and payoffs.\n"
        "- action='count', player='opp1', target_action=1: Counts how many times player chose action (0 or 1).\n"
        "- action='slice', start_round=10, end_round=20: Returns specific round range."
    )

    def __init__(self, match_rounds: list):
        self.match_rounds = match_rounds

    def execute(self, action: str, **kwargs) -> str:
        action = action.lower().strip()
        n = len(self.match_rounds)
        if n == 0:
            return "No completed rounds yet in this match."

        if action == "recent":
            k = int(kwargs.get("k", 5))
            k = min(max(k, 1), n)
            subset = self.match_rounds[-k:]
            lines = [f"R{r['round']:03d}: Actions={r['actions']} | Payoffs={r['payoffs']}" for r in subset]
            return "\n".join(lines)

        elif action == "count":
            player = str(kwargs.get("player", "opp1")).lower()
            target_act = int(kwargs.get("target_action", 1))
            p_idx = 0 if player in ["you", "me", "p0", "self"] else (1 if player in ["opp1", "p1"] else 2)
            count = sum(1 for r in self.match_rounds if r["actions"][p_idx] == target_act)
            pct = (count / n) * 100.0
            return f"Player {player} (idx {p_idx}) played Action {target_act} in {count}/{n} rounds ({pct:.1f}%)."

        elif action == "slice":
            start = max(0, int(kwargs.get("start_round", 0)))
            end = min(n, int(kwargs.get("end_round", n)))
            if start >= end:
                return f"Invalid range [{start}, {end}] for {n} available rounds."
            subset = self.match_rounds[start:end]
            lines = [f"R{r['round']:03d}: Actions={r['actions']} | Payoffs={r['payoffs']}" for r in subset]
            return "\n".join(lines)
        else:
            return f"Unknown history inspector action '{action}'."


class AgentToolbox:
    """
    Manages tool execution and formats system tool signatures for prompt injection.
    """
    def __init__(self, scratchpad: Optional[TournamentScratchpad] = None):
        self.scratchpad = scratchpad if scratchpad is not None else TournamentScratchpad()
        self.calculator = CalculatorTool()
        self.history_inspector = HistoryInspectorTool([])

    def update_match_history(self, match_rounds: list):
        self.history_inspector.match_rounds = match_rounds

    def get_tool_definitions(self) -> str:
        return (
            "Available Tools:\n"
            "1. calculator: Evaluates a mathematical expression (e.g. `calculator(expression='(110 - 95) / 2')`)\n"
            "2. tournament_scratchpad: Read/write persistent private notes that survive the entire tournament "
            "(e.g. `tournament_scratchpad(action='read')` or `tournament_scratchpad(action='write', content='...')`)\n"
            "3. history_inspector: Inspects visible past rounds (e.g. `history_inspector(action='recent', k=5)`)\n\n"
            "Tool Calling Protocol:\n"
            "To call a tool during your deliberation, write:\n"
            "TOOL_CALL: tool_name(arg1=val1, ...)\n"
            "You may call tools to verify math, check history, or update your strategic memory before choosing ACTION."
        )

    def execute_call(self, tool_call_str: str) -> str:
        tool_call_str = tool_call_str.strip()
        # Parse TOOL_CALL: tool_name(...)
        m = re.match(r"(?:TOOL_CALL:\s*)?([a-zA-Z_]+)\s*\((.*)\)", tool_call_str, re.DOTALL)
        if not m:
            return f"Error: Unable to parse tool call '{tool_call_str}'."
        tool_name = m.group(1).strip()
        raw_args = m.group(2).strip()

        # Parse kwargs
        kwargs = {}
        # Match arg=val or arg="val" or arg='val'
        arg_matches = re.findall(r"([a-zA-Z_]+)\s*=\s*(?:['\"](.*?)['\"]|([^,\)]+))", raw_args)
        for k, v_quoted, v_raw in arg_matches:
            val = v_quoted if v_quoted else v_raw.strip()
            # Try parsing numbers
            try:
                if "." in val: val = float(val)
                else: val = int(val)
            except ValueError:
                pass
            kwargs[k] = val

        if tool_name == "calculator":
            expr = kwargs.get("expression") or raw_args.strip("'\"")
            return self.calculator.execute(expression=str(expr))
        elif tool_name == "tournament_scratchpad":
            action = kwargs.pop("action", "read")
            content = kwargs.pop("content", "")
            return self.scratchpad.execute(action=str(action), content=str(content))
        elif tool_name == "history_inspector":
            action = kwargs.pop("action", "recent")
            return self.history_inspector.execute(action=str(action), **kwargs)
        else:
            return f"Error: Unknown tool '{tool_name}'."
