#!/usr/bin/env python3
"""Final confirmation on the sealed 200 (tests/clean/sealed.tsv): frozen control vs frozen
treatment. Sealed rule: totals only. Nothing per question is printed or written; ranks are
kept in memory and only aggregate counts leave this script."""
import csv, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import retrieval50 as R  # noqa: E402
from taskprogress import Task  # noqa: E402

SEALED = os.path.expanduser("~/repo-work/unllm/tests/clean/sealed.tsv")
BASE = {"UNLLM_PACKS": f"{R.AUDIT}/packs-en", "UNLLM_BOOSTS": "4,0.5", "UNLLM_MIN_MATCH": "n-1", "UNLLM_TOP": "50"}
ARMS = {"control": ("rerank", {}),
        "treatment": ("rerank,wide", {"UNLLM_W_TITLECOV": "0.15", "UNLLM_W_SIZE": "0.05"})}


def main():
    with open(SEALED) as f:
        rows = list(csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t"))
    qs = [{"id": r["id"], "question": r["question"], "pattern": r["answer_pattern"], "source": r["source_title"]} for r in rows]
    bar = Task("sealed", "Sealed 200 confirmation", total=len(qs) * len(ARMS))
    res = {}
    for arm, (layers, extra) in ARMS.items():
        arts, anss = [], []
        for q in qs:
            env = dict(os.environ, UNLLM_LAYERS=layers, **BASE, **extra)
            out = subprocess.run([R.CLI, "--passages", "50", q["question"]], capture_output=True, text=True,
                                 errors="replace", env=env, timeout=180).stdout
            ps = [l[10:] for l in out.splitlines() if l.startswith("[passage] ")]
            a, b = R.ranks(q, ps)
            arts.append(a)
            anss.append(b)
            bar.step(detail=arm)
        res[arm] = (arts, anss)
    bar.finish()
    lines = [f"\n### Sealed 200 confirmation (2026-10-19, night): totals only",
             "| arm | article (by source title) | first answer-bearing passage |", "|---|---|---|"]
    for arm, (arts, anss) in res.items():
        lines.append(f"| {arm} | {R.summary(arts)} | {R.summary(anss)} |")
    c, t = res["control"][1], res["treatment"][1]
    gained = sum(1 for x, y in zip(c, t) if y == 1 and x != 1)
    lost = sum(1 for x, y in zip(c, t) if x == 1 and y != 1)
    lines.append(f"Paired, answer paragraph first: treatment gained {gained}, lost {lost} (counts only), n = {len(qs)}.")
    text = "\n".join(lines)
    print(text)
    with open(R.REPORT, "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
