#!/usr/bin/env python3
"""Campaign summarizer: per-arm P(rank #1) / rank stats + CRN-paired diffs.

Input: one or more campaign JSONL files (one JSON object per run):
    {"run": i, "seed": s, "pool": [arm, opp, opp, ...], "scores": [...]}

The screened arm is at pool slot 0. Rank = 1 + #(strictly better opponents),
mirroring the harness's descending-score ranking with no tie-break (ties lose).

Usage:
    python summarize.py results/screening/prior_CandE_Hybrid.jsonl \
                        results/screening/prior_CandA_GTFT.jsonl ...

For each file: n, P(#1) with Wilson 95% CI, mean/median/worst rank, P(top-3),
mean arm score. Across files (same family, same seeds => CRN): paired score
diff vs. the first file with a normal-approximation CI and paired win fraction.
"""
import json
import math
import sys
from collections import defaultdict


def wilson(k, n, z=1.959963984540054):
    if n == 0:
        return (0.0, 0.0)
    p = k / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return ((c - h) / d, (c + h) / d)


def load(path):
    runs = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            scores = obj["scores"]
            arm = obj["pool"][0]
            opp_scores = scores[1:]
            rank = 1 + sum(1 for s in opp_scores if s > scores[0])
            best_opp = max(opp_scores)
            runs.append({
                "run": obj["run"],
                "arm": arm,
                "score": scores[0],
                "rank": rank,
                "margin_vs_best_opp": scores[0] - best_opp,
            })
    return runs


def stats(runs):
    n = len(runs)
    ranks = [r["rank"] for r in runs]
    firsts = sum(1 for r in ranks if r == 1)
    top3 = sum(1 for r in ranks if r <= 3)
    lo, hi = wilson(firsts, n)
    ranks_sorted = sorted(ranks)
    mean_rank = sum(ranks) / n
    med = (ranks_sorted[n // 2] if n % 2 else
           0.5 * (ranks_sorted[n // 2 - 1] + ranks_sorted[n // 2]))
    return {
        "n": n, "arm": runs[0]["arm"], "p1": firsts / n,
        "p1_lo": lo, "p1_hi": hi, "mean_rank": mean_rank, "med_rank": med,
        "worst_rank": max(ranks), "p_top3": top3 / n,
        "mean_score": sum(r["score"] for r in runs) / n,
        "p_beat_best": sum(1 for r in runs if r["margin_vs_best_opp"] > 0) / n,
    }


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    paths = argv[1:]
    per_file = {}
    for p in paths:
        runs = load(p)
        if not runs:
            print(f"{p}: EMPTY", file=sys.stderr)
            continue
        per_file[p] = runs

    base = per_file[paths[0]]
    header = (f"{'file':<46} {'arm':<16} {'n':>5} {'P(#1)':>7}"
              f" {'95% CI':<17} {'meanRk':>7} {'medRk':>6} {'worst':>5}"
              f" {'P(top3)':>7} {'meanSc':>8}")
    print(header)
    print("-" * len(header))
    for p in paths:
        if p not in per_file:
            continue
        s = stats(per_file[p])
        ci = f"[{s['p1_lo']:.3f},{s['p1_hi']:.3f}]"
        print(f"{p:<46} {s['arm']:<16} {s['n']:>5} {s['p1']:>7.3f}"
              f" {ci:<17} {s['mean_rank']:>7.1f} {s['med_rank']:>6.1f}"
              f" {s['worst_rank']:>5} {s['p_top3']:>7.3f}"
              f" {s['mean_score']:>8.2f}")

    # CRN-paired diffs vs the first file, aligned by run id.
    base_by_run = {r["run"]: r for r in base}
    if len(paths) > 1:
        print(f"\nCRN-paired score diffs (arm - {base[0]['arm']}, by run id):")
        print(f"{'file':<46} {'n':>5} {'meanD':>8} {'sdD':>7}"
              f" {'95% CI':<18} {'P(beat)':>7}")
        for p in paths[1:]:
            if p not in per_file:
                continue
            pairs = [(base_by_run[r["run"]]["score"], r["score"])
                     for r in per_file[p] if r["run"] in base_by_run]
            n = len(pairs)
            if n == 0:
                print(f"{p:<46} {'0':>5}  (no aligned run ids)")
                continue
            diffs = [b - a for a, b in pairs]
            mean = sum(diffs) / n
            var = sum((d - mean) ** 2 for d in diffs) / (n - 1) if n > 1 else 0.0
            sd = math.sqrt(var)
            se = sd / math.sqrt(n) if n > 0 else 0.0
            lo, hi = mean - 1.959963984540054 * se, mean + 1.959963984540054 * se
            wins = sum(1 for d in diffs if d > 0) / n
            print(f"{p:<46} {n:>5} {mean:>8.3f} {sd:>7.3f}"
                  f" [{lo:>7.3f},{hi:>7.3f}] {wins:>7.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
