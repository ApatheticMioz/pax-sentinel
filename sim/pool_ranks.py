#!/usr/bin/env python3
"""Per-archetype leaderboard from campaign JSONL (zero new compute).

Reads one or more campaign JSONL files that share the same pool composition
(same family, arms swapped at slot 0) and reports, per agent NAME:
    mean score, mean rank (within run, 1 = best), P(#1), occurrences.

Usage:
    python pool_ranks.py results/screening/prior_*.jsonl [--top N] [--bot M]
"""
import json
import sys
from collections import defaultdict


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("-") and
            (a.endswith(".jsonl") or a.endswith(".json") or "/" in a or
             "\\" in a)]
    top = 25
    bot = 5
    for i, a in enumerate(argv):
        if a == "--top":
            top = int(argv[i + 1])
        if a == "--bot":
            bot = int(argv[i + 1])
    if not args:
        print(__doc__)
        return 2

    sums = defaultdict(float)
    rank_sums = defaultdict(float)
    firsts = defaultdict(int)
    counts = defaultdict(int)
    for p in args:
        with open(p, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                obj = json.loads(line)
                pairs = sorted(zip(obj["pool"], obj["scores"]),
                               key=lambda t: -t[1])
                for i, (name, s) in enumerate(pairs, 1):
                    sums[name] += s
                    rank_sums[name] += i
                    counts[name] += 1
                    if i == 1:
                        firsts[name] += 1

    rows = []
    for name in counts:
        n = counts[name]
        rows.append((sums[name] / n, rank_sums[name] / n,
                     firsts[name] / n, n, name))
    rows.sort()  # ascending mean rank = best first

    print(f"{'rank':>4} {'name':<18} {'meanScore':>10} {'meanRk':>7}"
          f" {'P(#1)':>7} {'occ':>7}")
    print("-" * 60)
    for i, (m, r, p1, n, name) in enumerate(rows[:top], 1):
        print(f"{i:>4} {name:<18} {m:>10.1f} {r:>7.1f} {p1:>7.3f} {n:>7}")
    if bot:
        print("  ...")
        for i, (m, r, p1, n, name) in enumerate(rows[-bot:],
                                                len(rows) - bot + 1):
            print(f"{i:>4} {name:<18} {m:>10.1f} {r:>7.1f} {p1:>7.3f} {n:>7}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
