import urllib.request
import json
import re
import time
import random

# =====================================================================
# Payoff Tensor: payoff[my_action][(opp1_action, opp2_action)]
# As defined in ThreeWayPeaceWar.cpp
# =====================================================================
PAYOFF = {
    0: {  # Peace
        (0, 0): 4,
        (0, 1): 1,
        (1, 0): 1,
        (1, 1): -1,
    },
    1: {  # War
        (0, 0): 6,
        (0, 1): 3,
        (1, 0): 3,
        (1, 1): 0,
    }
}

# =====================================================================
# Algorithmic Contestants
# =====================================================================

class RampSentinelAgent:
    """
    Direct faithful translation of RampSentinelAgent.cpp
    Deterministic O(1) contrite-TFT backbone with frozen T=104 endgame ramp.
    Always emits silence into the public broadcast channel.
    """
    def __init__(self, name="RampSentinel (.cpp)"):
        self.name = name

    def select_action(self, n, my_hist, opp1_hist, opp2_hist):
        if n == 0:
            return 0  # R0: Open peace
        if opp1_hist[n - 1] == 1 and opp2_hist[n - 1] == 1:
            return 1  # R2: Double-war override
        if n >= 90 and n >= 104:
            return 1  # R3: Committed endgame ramp
        if my_hist[n - 1] == 1 and opp1_hist[n - 1] == 0 and opp2_hist[n - 1] == 0:
            return 0  # R1: Contrite repair of own unprovoked war
        return 1 if (opp1_hist[n - 1] == 1 or opp2_hist[n - 1] == 1) else 0

    def broadcast(self, n):
        return ""


class ProberDDCCAgent:
    """
    Direct port of ProberDDCC from sim/zoo/zoo_t2.cpp (Tier-2 strong submitter).
    Rounds 0..3: P, P, W, P.
    Round >= 4:
      - If both opponents never warred during probe: permanent War (farms saints).
      - If either opponent warred >= 2 times during probe: TFT_any (respects retaliators).
      - Otherwise: 35% war farming.
    """
    def __init__(self, name="ProberDDCC (Zoo-T2)"):
        self.name = name

    def select_action(self, n, my_hist, opp1_hist, opp2_hist):
        if n == 0: return 0
        if n == 1: return 0
        if n == 2: return 1  # Probe
        if n == 3: return 0

        o1_probe_wars = sum(opp1_hist[:4])
        o2_probe_wars = sum(opp2_hist[:4])

        if o1_probe_wars == 0 and o2_probe_wars == 0:
            return 1  # Exploit pure saints
        if o1_probe_wars >= 2 or o2_probe_wars >= 2:
            return 1 if (opp1_hist[n - 1] == 1 or opp2_hist[n - 1] == 1) else 0
        return 1 if random.random() < 0.35 else 0

    def broadcast(self, n):
        return ""


class ZDExtort3PAgent:
    """
    Direct port of ZDExtort3P from sim/zoo/zoo_t3.cpp (Tier-3 adversarial elite).
    Press & Dyson Zero-Determinant 3P extortionist.
    Opens with War (n=0).
    For n >= 1:
      p(peace) = 0.20 if last opp wars = 0
      p(peace) = 0.55 if last opp wars = 1
      p(peace) = 0.85 if last opp wars = 2
    """
    def __init__(self, name="ZDExtort3P (Zoo-T3)"):
        self.name = name

    def select_action(self, n, my_hist, opp1_hist, opp2_hist):
        if n == 0:
            return 1  # Opens War
        last_opp_wars = (1 if opp1_hist[n - 1] == 1 else 0) + (1 if opp2_hist[n - 1] == 1 else 0)
        if last_opp_wars == 0:
            p_peace = 0.20
        elif last_opp_wars == 1:
            p_peace = 0.55
        else:
            p_peace = 0.85
        return 0 if random.random() < p_peace else 1

    def broadcast(self, n):
        return ""


class SaintHunterAgent:
    """
    Direct port of SaintHunter from sim/zoo/zoo_t2.cpp (Tier-2 strong submitter).
    Rounds 0..14: Plays Peace (0).
    Rounds >= 15:
      If zero opponent wars across rounds 0..14: permanent War.
      Otherwise: plays TFT_any.
    """
    def __init__(self, name="SaintHunter (Zoo-T2)"):
        self.name = name

    def select_action(self, n, my_hist, opp1_hist, opp2_hist):
        if n < 15:
            return 0
        window_wars = sum(opp1_hist[:15]) + sum(opp2_hist[:15])
        tft = 1 if (opp1_hist[n - 1] == 1 or opp2_hist[n - 1] == 1) else 0
        if window_wars == 0:
            after_wars = sum(opp1_hist[15:]) + sum(opp2_hist[15:])
            return tft if after_wars >= 1 else 1
        return tft

    def broadcast(self, n):
        return ""


# =====================================================================
# Lean, Non-Biased Generative LLM Contestant
# =====================================================================

class ArenaLLMAgent:
    """
    Lean, unbiased LLM agent powered by local vLLM.
    Guarantees:
    - Zero meta-game hints, zero leading coaching
    - Ultra-lean per-round prompt tokens (~25 tokens/turn delta)
    - Full multi-turn prefix-caching alignment
    - 16k token ceiling (zero truncation)
    """
    def __init__(self, name, model="qwen3.8-27b", api_url="http://127.0.0.1:18020/v1/chat/completions",
                 temperature=0.6, max_tokens=16384):
        self.name = name
        self.model = model
        self.api_url = api_url
        self.temperature = temperature
        self.max_tokens = max_tokens
        self.messages = []
        self.last_reasoning = ""
        self.last_content = ""
        self.last_broadcast = ""
        self.tokens_used = 0

        # Lean, objective, non-redundant system prompt
        sys_msg = (
            "You are a strategic agent playing an iterated 3-player game: Three-Way Peace-War.\n\n"
            "Rules & Payoffs:\n"
            "- Actions each round: 0 (Peace) or 1 (War), chosen simultaneously.\n"
            "- Horizon: Hidden, uniform random on [90, 110] rounds.\n"
            "- If you play Peace (0): +4 if both opponents play Peace; +1 if one War, one Peace; -1 if both War.\n"
            "- If you play War (1):   +6 if both opponents play Peace; +3 if one War, one Peace;  0 if both War.\n\n"
            "Objective:\n"
            "Finish the match in 1st place with a strictly higher total score than both Opponent 1 and Opponent 2.\n"
            "A 3-way tie is a shared outcome, not an outright victory.\n\n"
            "Public Communication:\n"
            "Before each round, an optional public broadcast channel is available.\n"
            "You may broadcast a short public message (up to 20 words) to all players, or remain silent.\n"
            "Other players might choose to broadcast a message or remain silent as well.\n\n"
            "Output Format:\n"
            "Deliberate on your strategic position, then conclude at the end with:\n"
            "BROADCAST: <your message or NONE>\n"
            "ACTION: 0\n"
            "or\n"
            "ACTION: 1"
        )
        self.messages.append({"role": "system", "content": sys_msg})

    def select_action(self, n, last_outcome=None, incoming_broadcasts=None):
        if n == 0:
            user_msg = "Match starts: Round 0 of [90, 110] rounds. Output your opening BROADCAST and ACTION."
        else:
            b1 = (incoming_broadcasts.get("opp1", "") or "[Silence]").strip() if incoming_broadcasts else "[Silence]"
            b2 = (incoming_broadcasts.get("opp2", "") or "[Silence]").strip() if incoming_broadcasts else "[Silence]"

            # Ultra-lean, token-efficient, non-redundant round delta (~25-30 tokens)
            user_msg = (
                f"Round {n-1} Results: You={last_outcome['you']} | Opp1={last_outcome['opp1']} | Opp2={last_outcome['opp2']} "
                f"-> Payoff: {last_outcome['payoff']:+d} pts | Cumulative: You={last_outcome['my_tot']:.0f}, Opp1={last_outcome['opp1_tot']:.0f}, Opp2={last_outcome['opp2_tot']:.0f}\n"
                f"Broadcasts for Round {n}: Opp1=\"{b1}\" | Opp2=\"{b2}\"\n"
                f"Choose for Round {n}:"
            )

        self.messages.append({"role": "user", "content": user_msg})

        payload = {
            "model": self.model,
            "messages": self.messages,
            "max_tokens": self.max_tokens,
            "temperature": self.temperature
        }

        for attempt in range(3):
            try:
                req = urllib.request.Request(
                    self.api_url,
                    data=json.dumps(payload).encode("utf-8"),
                    headers={"Content-Type": "application/json"}
                )
                with urllib.request.urlopen(req, timeout=180) as resp:
                    data = json.loads(resp.read().decode("utf-8"))

                choice = data["choices"][0]["message"]
                usage = data.get("usage", {})
                self.tokens_used = usage.get("completion_tokens", 0)
                reasoning = choice.get("reasoning") or ""
                content = choice.get("content") or ""
                self.last_reasoning = reasoning.strip()
                self.last_content = content.strip()

                # Parse broadcast
                bc_match = re.search(r"BROADCAST\s*:\s*(.+)", content, re.IGNORECASE)
                if bc_match:
                    bc_text = bc_match.group(1).strip()
                    self.last_broadcast = "" if bc_text.upper() in ["NONE", "SILENCE", "EMPTY", "N/A", "[SILENCE]"] else bc_text
                else:
                    self.last_broadcast = ""

                # Parse action
                act = self._extract_action(content, reasoning)
                
                # Keep multi-turn assistant turn clean and compact in message history
                # This ensures prompt history stays concise without bloat while maintaining prefix cache
                clean_reply = f"BROADCAST: {self.last_broadcast if self.last_broadcast else 'NONE'}\nACTION: {act}"
                self.messages.append({"role": "assistant", "content": clean_reply})
                return act

            except Exception as e:
                time.sleep(1.0)
                if attempt == 2:
                    print(f"[{self.name}] Error after 3 attempts: {e}. Defaulting to Peace(0).")
                    return 0

    def _extract_action(self, content, reasoning):
        combined = content + "\n" + reasoning
        matches = re.findall(r"ACTION\s*:\s*([01])", combined, re.IGNORECASE)
        if matches:
            return int(matches[-1])
        if "action: 1" in combined.lower() or "action 1" in combined.lower():
            return 1
        if "action: 0" in combined.lower() or "action 0" in combined.lower():
            return 0
        return 0
