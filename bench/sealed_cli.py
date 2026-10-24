#!/usr/bin/env python3
"""End-to-end answers on the sealed 200 (tests/clean/sealed.tsv), totals only. Runs the plain CLI
(build/unllm QUESTION) with whatever environment it is given, scores the first paragraph against
the answer pattern, and prints ONE line: the count right. Nothing per question is printed or kept.
  sealed_cli.py LABEL"""
import csv, os, re, subprocess, sys
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

CLI = os.environ.get("UNLLM_CLI") or os.path.expanduser("~/repo-work/unllm/build/unllm")
SEALED = os.path.expanduser("~/repo-work/unllm/tests/clean/sealed.tsv")


def main(label):
    with open(SEALED) as f:
        rows = list(csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t"))
    right, bar = 0, Task("sealedcli", f"Sealed 200, answers: {label}", total=len(rows))
    for r in rows:
        out = subprocess.run([CLI, r["question"]], capture_output=True, text=True, errors="replace", timeout=1800).stdout
        ans = " ".join(out.split("\n\n")[0].split())
        right += bool(ans) and re.search(r["answer_pattern"], ans, re.I) is not None
        bar.step()
    bar.finish()
    line = f"- sealed 200, {label}: {right} of {len(rows)} right"
    print(line)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write(line + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
