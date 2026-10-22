#!/usr/bin/env python3
"""Sealed 200, totals only: production defaults before (saved pre-merge binary) and after the
higher-score merge. Nothing per question is printed or written."""
import csv, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import retrieval50 as R  # noqa: E402
from taskprogress import Task  # noqa: E402

SEALED = os.path.expanduser("~/repo-work/unllm/tests/clean/sealed.tsv")
ARMS = {"before (pre-merge binary)": f"{R.AUDIT}/bin/unllm-premerge", "after (merge change)": R.CLI}


def main():
    with open(SEALED) as f:
        rows = list(csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t"))
    qs = [{"question": r["question"], "pattern": r["answer_pattern"], "source": r["source_title"]} for r in rows]
    env = {k: v for k, v in os.environ.items() if not k.startswith("UNLLM")} | {"UNLLM_TOP": "50"}
    bar = Task("sealedmerge", "Sealed 200: merge change", total=len(qs) * len(ARMS))
    res = {}
    for arm, binary in ARMS.items():
        a1, b1 = [], []
        for q in qs:
            out = subprocess.run([binary, "--passages", "50", q["question"]], capture_output=True, text=True,
                                 errors="replace", env=env, timeout=180).stdout
            a, b = R.ranks(q, [l[10:] for l in out.splitlines() if l.startswith("[passage] ")])
            a1.append(a)
            b1.append(b)
            bar.step(detail=arm)
        res[arm] = (a1, b1)
    bar.finish()
    lines = ["\n### Sealed 200, higher-score merge (2026-10-19, night): totals only, production defaults",
             "| arm | article (by source title) | first answer-bearing passage |", "|---|---|---|"]
    for arm, (a1, b1) in res.items():
        lines.append(f"| {arm} | {R.summary(a1)} | {R.summary(b1)} |")
    (x, y) = (res[k][1] for k in ARMS)
    lines.append(f"Paired, answer paragraph first: after gained {sum(1 for p, q in zip(x, y) if q == 1 and p != 1)}, "
                 f"lost {sum(1 for p, q in zip(x, y) if p == 1 and q != 1)} (counts only), n = {len(qs)}.")
    text = "\n".join(lines)
    print(text)
    with open(R.REPORT, "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
