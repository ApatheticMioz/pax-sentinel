import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import argparse
import random
from .agents import (
    RampSentinelAgent,
    ProberDDCCAgent,
    ZDExtort3PAgent,
    SaintHunterAgent,
    ArenaLLMAgent,
)
from .runner import run_arena_match

def build_roster(config_id):
    """
    Roster configurations:
    1: Dual-LLM Society   (RampSentinel vs LLM-Alpha vs LLM-Beta)
    2: Crossfire Crucible (RampSentinel vs ProberDDCC vs LLM-Gamma)
    3: Extortionist       (RampSentinel vs ZDExtort3P vs LLM-Delta)
    4: Saint Hunter       (RampSentinel vs SaintHunter vs LLM-Epsilon)
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

def main():
    parser = argparse.ArgumentParser(description="Three-Way Peace-War Arena Tournament Runner")
    parser.add_argument("--config", type=int, default=1, choices=[1, 2, 3, 4],
                        help="Roster config: 1=Dual-LLM, 2=ProberDDCC, 3=ZDExtort3P, 4=SaintHunter")
    parser.add_argument("--rounds", type=int, default=None,
                        help="Exact rounds (default: uniformly sampled from [90, 110])")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed for horizon sampling and stochastic agents")
    parser.add_argument("--match-id", type=str, default="1",
                        help="Identifier for the match output")
    args = parser.parse_args()

    random.seed(args.seed)
    horizon = args.rounds if args.rounds is not None else random.randint(90, 110)

    p0, p1, p2 = build_roster(args.config)
    run_arena_match(args.match_id, p0, p1, p2, horizon)

if __name__ == "__main__":
    main()
