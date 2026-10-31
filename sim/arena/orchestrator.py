import sys
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8")

import os
import time
import json

from .induction_clean import run_zero_hint_sequential_trial
from .induction_xhigh import run_zero_hint_xhigh_trial
from .branch_runner import NOMINATED_BRANCH_POINTS, run_counterfactual_branch
from .ablation_runner import NOMINATED_ABLATION_MATCHES, run_ablation_match
from .tournament_tool_agent import run_tool_tournament

def main():
    t_master_start = time.time()
    print("=" * 95)
    print("AI4012 ARENA MASTER EXPERIMENTAL ORCHESTRATOR")
    print("Executing Approved Multi-Tier Empirical Campaign:")
    print("1. Tier 2 Spotlight: Zero-Hint Induction (xhigh 32k vs medium)")
    print("2. Tier 2: Counterfactual xhigh (32k) Inflection Branching (8 Nominated Points)")
    print("3. Tier 3: Tool-Augmented Tournament with Persistent Cross-Match Scratchpad")
    print("4. Tier 1: Reasoning Factorial Ablation (none vs low vs medium on 12 Matches)")
    print("=" * 95)

    # -------------------------------------------------------------
    # STAGE 1: Zero-Hint Induction under xhigh (32k tokens)
    # -------------------------------------------------------------
    print("\n" + "#" * 95)
    print("STAGE 1: ZERO-HINT INDUCTION UNDER XHIGH (32k TOKENS)")
    print("#" * 95)
    for t in [1, 2]:
        run_zero_hint_xhigh_trial(trial_id=t, temperature=0.6)

    # -------------------------------------------------------------
    # STAGE 2: Counterfactual xhigh (32k tokens) Branch Replays
    # -------------------------------------------------------------
    print("\n" + "#" * 95)
    print("STAGE 2: COUNTERFACTUAL XHIGH (32k) BRANCH REPLAYS (8 Nominated Points)")
    print("#" * 95)
    for spec in NOMINATED_BRANCH_POINTS:
        run_counterfactual_branch(spec)

    # -------------------------------------------------------------
    # STAGE 3: Tool-Augmented Tournament with Scratchpad Memory
    # -------------------------------------------------------------
    print("\n" + "#" * 95)
    print("STAGE 3: TOOL-AUGMENTED TOURNAMENT WITH PERSISTENT SCRATCHPAD")
    print("#" * 95)
    run_tool_tournament(config_id=1, num_matches=5)

    # -------------------------------------------------------------
    # STAGE 4: Reasoning Factorial Ablation (none vs low)
    # -------------------------------------------------------------
    print("\n" + "#" * 95)
    print("STAGE 4: REASONING FACTORIAL ABLATION (none vs low)")
    print("#" * 95)
    for item in NOMINATED_ABLATION_MATCHES:
        for mode in ["none", "low"]:
            run_ablation_match(item["cfg"], item["m"], mode=mode)

    total_time = time.time() - t_master_start
    print("\n" + "=" * 95)
    print(f"ALL EXPERIMENTAL STAGES CONCLUDED IN {total_time:.1f}s ({total_time/3600:.2f} hours)")
    print("=" * 95)

if __name__ == "__main__":
    main()

