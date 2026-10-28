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
      - If both opponents never warred during probe (saints): permanent War (farms them).
      - If either opponent warred >= 2 times during probe: TFT_any (respects retaliators).
      - Otherwise: non-periodic 35% war farming.
    Always emits silence into the public broadcast channel.
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
      Draws p(peace) = 0.20 if last opp wars = 0 (p(war) = 0.80)
      Draws p(peace) = 0.55 if last opp wars = 1 (p(war) = 0.45)
      Draws p(peace) = 0.85 if last opp wars = 2 (p(war) = 0.15)
    Always emits silence into the public broadcast channel.
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
      If zero opponent wars across rounds 0..14: commits to permanent War.
      Otherwise: plays TFT_any.
    Always emits silence into the public broadcast channel.
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
# Unbiased Generative LLM Contestant
# =====================================================================

class ArenaLLMAgent:
    """
    Unbiased Generative Game-Theoretic Agent powered by local vLLM.
    Features:
    - Persistent multi-turn chat history (full vLLM prefix cache reuse)
    - 16,384 max tokens ceiling (zero-truncation guarantee)
    - Natural, unprompted strategic deliberation
    - Optional broadcast channel without leading suggestions or biases
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

        # Revised, completely unbiased system prompt
        sys_msg = (
            "You are an autonomous strategic agent competing in an iterated 3-player game: Three-Way Peace-War.\n\n"
            "=== Game Rules & Dynamics ===\n"
            "1. In every round, all 3 players simultaneously choose one action:\n"
            "   - 0 : Peace\n"
            "   - 1 : War\n\n"
            "2. Match Horizon:\n"
            "   The exact match length is hidden from all players. It is drawn uniformly at random between 90 and 110 rounds.\n\n"
            "3. Payoff Matrix for your chosen action in each round:\n"
            "   - If you choose Peace (0):\n"
            "       +4 points if BOTH opponents choose Peace\n"
            "       +1 point  if ONE opponent chooses War, ONE chooses Peace\n"
            "       -1 point  if BOTH opponents choose War\n"
            "   - If you choose War (1):\n"
            "       +6 points if BOTH opponents choose Peace\n"
            "       +3 points if ONE opponent chooses War, ONE chooses Peace\n"
            "        0 points if BOTH opponents choose War\n\n"
            "=== Objective ===\n"
            "Your objective is to finish the match in 1st place with a higher total score than both Opponent 1 and Opponent 2.\n"
            "A 3-way tie is a shared outcome, not an outright victory.\n\n"
            "=== Public Communication ===\n"
            "Before each round, an optional public broadcast channel is available.\n"
            "You may choose to broadcast a short public message (up to 20 words) to all players, or remain silent.\n"
            "Other players might choose to broadcast a message or remain silent as well.\n\n"
            "=== Format Instructions ===\n"
            "Deliberate on your strategic position and decide your choice for the upcoming round.\n"
            "At the very end of your response, output your decision in the exact format:\n"
            "BROADCAST: <your message or NONE>\n"
            "ACTION: 0\n"
            "or\n"
            "ACTION: 1"
        )
        self.messages.append({"role": "system", "content": sys_msg})

    def select_action(self, n, last_outcome=None, incoming_broadcasts=None):
        if n == 0:
            user_msg = (
                "Match starts. This is Round 0 of approximately 100 rounds (horizon in [90, 110]).\n"
                "Choose your opening broadcast and action."
            )
        else:
            b1 = (incoming_broadcasts.get("opp1", "") or "[Silence]").strip() if incoming_broadcasts else "[Silence]"
            b2 = (incoming_broadcasts.get("opp2", "") or "[Silence]").strip() if incoming_broadcasts else "[Silence]"

            user_msg = (
                f"Round {n-1} Results:\n"
                f"- Actions: You played {last_outcome['you']} | Opponent 1 played {last_outcome['opp1']} | Opponent 2 played {last_outcome['opp2']}\n"
                f"- Round Payoff: You earned {last_outcome['payoff']:+d} points\n"
                f"- Cumulative Scores: You = {last_outcome['my_tot']:.0f} | Opponent 1 = {last_outcome['opp1_tot']:.0f} | Opponent 2 = {last_outcome['opp2_tot']:.0f}\n\n"
                f"Incoming Broadcasts for Round {n}:\n"
                f"- Opponent 1: \"{b1}\"\n"
                f"- Opponent 2: \"{b2}\"\n\n"
                f"Evaluate the state and decide your choice for Round {n} of approximately 100 rounds."
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
                with urllib.request.urlopen(req, timeout=60) as resp:
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
                self.messages.append({"role": "assistant", "content": content if content else f"ACTION: {act}"})
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
