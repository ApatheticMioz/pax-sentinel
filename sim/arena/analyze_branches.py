import glob
import json
import os

def analyze():
    files = sorted(glob.glob("sim/arena/logs_xhigh_branch/branch_*.json"))
    print(f"Total Completed Counterfactual Branches: {len(files)}")
    print("=" * 80)
    flipped_count = 0
    for f in files:
        with open(f, "r", encoding="utf-8") as fp:
            d = json.load(fp)
        b_id = d.get("branch_id")
        spec = d.get("branch_spec", {})
        flipped = d.get("winner_flipped")
        deltas = d.get("score_delta")
        orig_w = d.get("original_winners")
        new_w = d.get("counterfactual_winners")
        orig_s = d.get("original_scores")
        new_s = d.get("counterfactual_scores")
        if flipped:
            flipped_count += 1
        print(f"[{b_id}] Round {spec.get('branch_round')} ({spec.get('name')})")
        print(f"  Flipped: {flipped} | Rationale: {spec.get('rationale')[:80]}...")
        print(f"  Original: {orig_w} with {orig_s}")
        print(f"  Branch:   {new_w} with {new_s}")
        print(f"  Deltas:   {deltas}")
        print("-" * 80)
    print(f"\nSummary: {flipped_count}/{len(files)} branches flipped match winners.")

if __name__ == "__main__":
    analyze()
