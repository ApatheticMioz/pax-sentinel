# Literature Ledger — Three-Way Peace-War Tournament Agent

Consolidated from six coworker research slices (recon, IPD canon, N≥3 theory, opponent-modeling/ZD,
tournament meta-dynamics, classmate calibration). Citation status: **[V]** = verified this session
via OpenAlex/DOI/arXiv fetch; **[M]** = canonical, cited from domain knowledge, re-verify before
formal publication. Every claim that shapes a shipped design decision is listed here or in
`GAME-ANALYSIS.md` with its grounding.

## 1. Game ground truth (code, not literature)

Source: `ThreeWayPeaceWar.cpp` (the only executable ground truth). See `GAME-ANALYSIS.md` §1.
Instructions.txt conflicts with the code on the `selectAction` const-signature; the code wins
(we submit that file's harness semantics).

## 2. IPD canon & strategy zoo

| Result | Use to us | Source |
|---|---|---|
| TFT won Axelrod 1980 via *nice/retaliatory/forgiving/clear*; total-score ≠ pairwise-win | Property checklist for our backbone | Axelrod 1984 *Evolution of Cooperation* [M] |
| Pavlov/WSLS beats TFT in noisy/population settings | Considered & rejected for 3P (see GAME-ANALYSIS §5) | Nowak & Sigmund, *Nature* 364:257, 1993 [M]; Imhof/Fudenberg/Nowak, JTB 245:404, 2007 [V] |
| CTFT/GTFT: generosity & contrition tuned to noise | Backbone forgiveness design | Wu & Axelrod, JCR 39:491, 1995 [M] |
| Gradual: escalating punishment + calm-down won ALIFE V | Escalation + amnesty design | Beaufils/Delahaye/Mathieu, ALIFE V 1996 [M] |
| **Prober/exploiter hybrids win total-score round-robins**; probe fingerprints (DDCC-class) classify in 4–6 rounds; optimal play depends on pool composition | Core of our candidate E | Li/Hingston/Kendall, IEEE TCIAIG 3(4):348, 2011 [M] |
| Fingerprinting response-to-probe vectors classify families | Classifier design | Ashlock et al., IEEE TEVC 2007 [M] |

## 3. N≥3-player PD

| Result | Use to us | Source |
|---|---|---|
| NIPD payoff-by-count formulation; cooperation harder as N grows; k-index threshold strategies | Backbone trigger design | Hamburger, J. Math. Sociology 3:27, 1973 [V]; Hardin, *Science* 162:1243, 1968 [V]; Olson 1965 [M] |
| 3-person IPD: retaliation **3-cycles**, punishment externalities, collapse sensitivity | Stochastic leniency to break cycles | Matsushima & Ikegami, JTB 195:53, 1998 [V] |
| Winning N≥3 family: per-opponent memory, lenient threshold, **probabilistic retaliation**, bounded amnesty, reputation weighting | Candidate E backbone | synthesis over [V]/[M] above |
| Second-order free-riding on punishment; amnesty as mitigation | Amnesty window design | Ohtsuki/Iwasa lineage [M] |
| Reputation/causal-attribution: punish persistent defectors longer, forgive one-offs fast | Per-opponent reputation weights | Liebrand & Van Lange 1988 [V] |
| Triad 2-vs-1 implicit coalition logic; never punish the innocent coalition member | Class-conditional pair policy | balance-of-power lineage (Schelling 1960/66) [M] |

## 4. Opponent modeling, ZD, endgame

| Result | Use to us | Source |
|---|---|---|
| ZD extortion: dominant 1-on-1, **diluted at N=3** (pins only the *average* of others), unstable in pools; generous ZD → GTFT limit | No extortion; generous/competitive backbone | Press & Dyson, PRL 108:058102, 2012 [V]; Hilbe/Wu/Traulsen/Nowak, JTB 367:39, 2015 [V]; Stewart & Plotkin, PRL 111:068101, 2013 [M]; Hilbe/Traulsen/Sigmund, GEB 2015 [V] |
| Noise kills classical extortion (no clean ZD under noise) | Not our regime anyway (no channel noise) | Hao/Rong/Zhou, PRE 91:052803, 2015 [V] |
| Probe-sequence classification 10–20 rounds for 7 memory-1 archetypes; uncertainty → GTFT maximin fallback | Two-test classifier + fallback | Ashlock CIG 2006 [V]; Li et al. 2011 [M] |
| ZD in finitely repeated games needs discount threshold; endgame unraveling principled under known horizon | Endgame ramp | Ichinose & Masuda, JTB 447:1, 2018 [V] |
| Anti-collusion: never cooperate when all others defected | Anti-lock rule | Slany & Kienreich 2006 [M] |
| Entropy/autocorrelation detectors catch random/cyclic opponents | Cheap classifier features | standard practice [M] |

## 5. Tournament meta-dynamics & evaluation methodology

| Result | Use to us | Source |
|---|---|---|
| Total-score tournaments won by *harvest-from-weak, cap-downside-vs-strong* profiles | Exploitation layer rationale | Glynatsi/Knight/Harper, PLOS Comput Biol 2024 [V]; Harper et al., PLoS ONE 12:e0188046, 2017 [V] |
| Extortion fragile in heterogeneous pools; "competitive" (never below co-players) robust | No extortion; bounded envy | Hilbe/Nowak/Sigmund, PNAS 2013 [V]; Knight et al., PLoS ONE 2024 [V] |
| Single rerun rankings noisy (top seed-dependent; ×25,000 replication); unfamous strategies win; **never ship a recognizable named strategy** | Robust wide-basin + novel parameters | Knight et al., Comm AI 2026 (Axelrod-2nd ×25k) [V] |
| Winning property set: nice + provocable + forgiving + a-little-envious + adaptive | Shipped property checklist | consolidated [V] above |
| Evaluation: stratified pool Monte-Carlo, **hold-out compositions**, adversarial augmentation, common random numbers, rank-distribution reporting (P(#1), lower quantiles), never eval-on-training-pool | Our entire campaign protocol | Knight 2026 [V]; Gleave & Russell 2017 [V]; Simons & Yao, Ann. Stat. 1999 [M]; Turner & Firth, JSS 48(9), 2012 [M] |
| Meta-depth ~1.5 levels: model pool distribution, not opponents' minds; Red King instability caps mind-reading | No deep opponent-meta modeling | Hilbe 2013 [V] |
| ANAC/TAC: adapt-to-field winners; overfit-to-last-year degrades | Hold-out + adversarial tier | Baarslag 2011 [M]; Collins/Ketter/Sadeh, AI Mag 2010 [M] |

## 6. Classmate-submission calibration (constructed prior — planning ranges, not measurement)

No public prior-offering repos exist (verified; nothing fabricated). Prior built from: the 6
sample bots as copy-anchor, the carykh public crowd tournament (joss/ftft/detective/simpleton
added to zoo), classroom-IPD writeups (Oyun 2012 [V]; JECE 1998 [V]), LLM strategic-fingerprint
analysis (arXiv:2507.02618 [V]; Tyrrell et al. arXiv:2001.05911 [V]).

| Archetype | Est. share | Note |
|---|---|---|
| naive TFT family | 20–30% | incl. distinct **oppHistory1-only broken-TFT** |
| tolerant/majority retaliators | 10–20% | strongest realistic classmate family |
| random/Joss probabilists | 10–15% | |
| all-Peace saints | 5–10% | primary prey |
| all-War | 3–5% | |
| grim (often state-buggy) | 5–10% | |
| Freaky/constant | 3–5% | |
| probers/exploiters | 3–5% | the real rivals |
| overcomplicated lookup agents | 5–10% | bimodal; some crash on n==0 |
| broken/mis-named | 2–8% | don't play → simulate both pool sizes |

Implication: the modal pool is short-memory conditional cooperators ⇒ backbone ≈ tolerant-family
is *necessary but not sufficient*; ranking edge comes from the exploitation layer (saint-commit,
r\*-farming) + robustness (anti-lock, dithering, endgame) — validated at 10⁵-tournament scale.

## 7. Design-stage quantitative synthesis (qwen xhigh brainstorm — priors to certify)

Probe EV = +2 − 4·E[trap] ⇒ never mid-horizon vs responsive pairs, always vs saints,
endgame-only vs responsive; two-test protocol worth ≈ +50/match vs trickle-only, ≈ +38 vs GTFT
baseline in the reference pool; r\* ≈ 0.4 Tolerant-farming rate is the killer parameter
(first-crossing flip risk ≤1–2%); dithered amnesty ±1 round + ε≈0.2 breaks 3-cycles/cycle-fitters
at ≤1–2% EV cost; WSLS-3P quantitatively killed. All certified/killed by the campaign (Phase C).
