#!/usr/bin/env python3
"""Generate pool manifests for the campaign families (POOLS.md section 1).

Each manifest line: "name [count]". `prior`-family manifests hold 99 opponent
slots; the screened candidate arm is appended at slot 0 by the campaign runner
wrapper (see run_screening.sh), keeping opponent composition identical across
arms (CRN at the pool level).
"""
import os
import sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pools")

FAMILIES = {
    # Calibrated classmate prior (research/LITERATURE.md section 6).
    "prior": [
        ("TFT_any", 10), ("TFT_maj", 5), ("BrokenTFT_1", 4), ("Joss10", 4),
        ("TF2T_any", 4),
        ("TolerantAgent", 7), ("TolerantW5", 7), ("TolerantW10", 4),
        ("RandomAgent", 8), ("NoisyP20", 3), ("NoisyP80", 3),
        ("NiceAgent", 5), ("Freaky25", 3),
        ("NastyAgent", 4),
        ("Grim_any", 4), ("Grim2Any", 2), ("GrimAnyForgive1", 2),
        ("Freaky75", 3),
        ("Alternator", 3), ("PeriodicPPW", 3),
        ("Prober_DDCC", 2), ("Detective", 2),
        ("Gradual3P", 2), ("CTFT3P", 1), ("KLenient2", 1), ("HybridA", 1),
        ("EndgameGTFT", 1), ("Omega3P", 1),
    ],
    # Exploitation upside: saints + tolerant + TFT mass.
    "nice-heavy": [
        ("NiceAgent", 12), ("Freaky25", 6),
        ("TolerantAgent", 10), ("TolerantW5", 10), ("TolerantW10", 8),
        ("TFT_any", 12), ("TFT_maj", 8), ("TF2T_any", 6), ("Joss10", 5),
        ("RandomAgent", 8), ("NoisyP20", 4),
        ("Grim_any", 4), ("Alternator", 3), ("PeriodicPPW", 3),
    ],
    # Cascade risk: retaliators dominate.
    "retal-heavy": [
        ("Grim_any", 12), ("Grim_maj", 6), ("Grim2Any", 6),
        ("GrimAnyForgive1", 5), ("AntiExploit", 6),
        ("TFT_any", 12), ("TFT_maj", 8), ("TF2T_any", 6), ("TF2T_maj", 5),
        ("KLenient2", 5), ("KLenient3", 4), ("Gradual3P", 4), ("CTFT3P", 4),
        ("Omega3P", 4), ("EndgameGTFT", 4), ("TolerantAgent", 8),
        ("NastyAgent", 6), ("RandomAgent", 5),
    ],
    # Rivalry: exploiters hunting the same prey.
    "exploit-heavy": [
        ("Prober_DDCC", 6), ("Detective", 6), ("SaintHunter", 6),
        ("HybridA", 5), ("HybridB", 5), ("MetaV1", 4), ("ZDExtort3P", 4),
        ("GenZD3P", 4), ("CycleFitter", 4), ("FreqLearner", 4),
        ("MarkovLearner", 4), ("CounterProber", 4), ("AdaptModel5", 4),
        ("NiceAgent", 8), ("Freaky25", 4), ("TolerantAgent", 8),
        ("TolerantW5", 8), ("TFT_any", 8), ("NastyAgent", 4),
        ("RandomAgent", 6),
    ],
    # Displacement: elites + meta-bots hunting us specifically.
    "elite-heavy": [
        ("MetaV1", 8), ("CounterProber", 8), ("AdaptModel5", 8),
        ("ColludeSqueeze", 6), ("ZDExtort3P", 6), ("Prober_DDCC", 6),
        ("HybridA", 6), ("HybridB", 6), ("SaintHunter", 6), ("CycleFitter", 6),
        ("FreqLearner", 6), ("MarkovLearner", 6), ("KLenient2", 6),
        ("KLenient3", 6), ("Gradual3P", 5), ("CTFT3P", 5), ("Omega3P", 5),
        ("AntiExploit", 5), ("EndgameGTFT", 5), ("T4Tsmart", 5),
    ],
    # Coverage: every registered zoo bot at least once.
    "uniform-zoo": [
        ("NiceAgent", 2), ("NastyAgent", 2), ("RandomAgent", 2),
        ("TolerantAgent", 2), ("FreakyAgent", 1), ("T4TAgent", 2),
        ("TFT_any", 2), ("TFT_maj", 2), ("TF2T_any", 1), ("TF2T_maj", 1),
        ("Grim_any", 2), ("Grim_maj", 1), ("GTFT_any_g10", 1),
        ("GTFT_any_g30", 1), ("WSLS_naive", 1), ("Alternator", 1),
        ("PeriodicPPW", 1), ("Freaky25", 1), ("Freaky75", 1),
        ("BrokenTFT_1", 2), ("Joss10", 1), ("Detective", 1), ("NoisyP20", 1),
        ("NoisyP80", 1), ("TolerantW5", 2), ("TolerantW10", 2),
        ("PavlovMaj", 1), ("RandomWalk", 1), ("Grim2Any", 1),
        ("GrimAnyForgive1", 1), ("MajWindow3", 1), ("TitFor2TatAvg", 1),
        ("SuckerFirst", 1), ("Prober_DDCC", 1), ("SaintHunter", 1),
        ("Gradual3P", 1), ("CTFT3P", 1), ("Omega3P", 1), ("GenZD3P", 1),
        ("KLenient2", 1), ("KLenient3", 1), ("EndgameGTFT", 1), ("HybridA", 1),
        ("HybridB", 1), ("FreqLearner", 1), ("MarkovLearner", 1),
        ("CycleFitter", 1), ("AntiExploit", 1), ("T4Tsmart", 1),
        ("ZDExtort3P", 1), ("CounterProber", 1), ("AdaptModel5", 1),
        ("ColludeSqueeze", 1), ("MetaV1", 1),
    ],
    # Mode-of-field: canonical LLM-paste archetypes only.
    "llm-pattern": [
        ("TFT_any", 25), ("BrokenTFT_1", 10), ("TolerantAgent", 15),
        ("TolerantW5", 10), ("Joss10", 8), ("Grim_any", 8), ("NiceAgent", 8),
        ("NastyAgent", 5), ("Freaky25", 5), ("RandomAgent", 5),
    ],
    # Pool-size robustness: prior composition at half size.
    "small-pool": [
        ("TFT_any", 6), ("TFT_maj", 2), ("BrokenTFT_1", 2), ("Joss10", 2),
        ("TF2T_any", 2),
        ("TolerantAgent", 3), ("TolerantW5", 4), ("TolerantW10", 3),
        ("RandomAgent", 4), ("NoisyP20", 2), ("NoisyP80", 1),
        ("NiceAgent", 3), ("Freaky25", 1),
        ("NastyAgent", 2),
        ("Grim_any", 2), ("Grim2Any", 1), ("GrimAnyForgive1", 1),
        ("Freaky75", 2),
        ("Alternator", 1), ("PeriodicPPW", 2),
        ("Prober_DDCC", 1), ("Detective", 1),
        ("Gradual3P", 1), ("CTFT3P", 1), ("KLenient2", 1), ("HybridA", 1),
    ],
}


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    ok = True
    for family, entries in FAMILIES.items():
        total = sum(c for _, c in entries)
        path = os.path.join(OUT, f"{family}.txt")
        with open(path, "w", encoding="utf-8") as f:
            for name, count in entries:
                f.write(f"{name} {count}\n" if count != 1 else f"{name}\n")
        print(f"{family}: {total} opponent slots -> {path}")
    # uniform-zoo should cover every non-candidate name exactly >=1; small-pool
    # must stay <= 60 opponents (campaign cap 200 total incl. arm).
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
