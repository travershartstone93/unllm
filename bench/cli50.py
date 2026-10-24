#!/usr/bin/env python3
"""End-to-end answers from the plain CLI on a question set (DATA/audit/NAME.tsv): the first
paragraph against the answer pattern. Per-question rows to DATA/audit/cli-NAME-LABEL.jsonl, the
total to the audit report.
  cli50.py LABEL NAME [NAME ...]"""
import json, os, re, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.environ.get("UNLLM_CLI") or os.path.expanduser("~/repo-work/unllm/build/unllm")


def main(label, names):
    lines = [f"\n### Plain CLI, end to end: {label} ({time.strftime('%Y-%m-%d %H:%M')})"]
    for name in names:
        rows = [dict(zip(("id", "question", "pattern"), l.split("\t")[:3])) for l in open(f"{A}/{name}.tsv").read().splitlines()[1:]]
        right, secs = 0, 0.0
        out = open(f"{A}/cli-{name}-{label}.jsonl", "w")
        bar = Task("cli50", f"CLI {label}: {name}", total=len(rows))
        for q in rows:
            t0 = time.time()
            res = subprocess.run([CLI, "--branch", q["question"]], capture_output=True, text=True, errors="replace", timeout=1800).stdout
            secs += time.time() - t0
            ans = " ".join(res.split("\n\n")[0].split())
            branch = next((l[9:-1] for l in res.splitlines() if l.startswith("[branch: ")), "")
            ans = ans.replace(f"[branch: {branch}]", "").strip()
            ok = bool(ans) and re.search(q["pattern"], ans, re.I) is not None
            right += ok
            out.write(json.dumps({"id": q["id"], "question": q["question"], "pattern": q["pattern"], "right": ok, "branch": branch, "answer": ans[:600]}) + "\n")
            out.flush()
            bar.step(detail=q["id"])
        bar.finish()
        lines.append(f"- {name}: {right} of {len(rows)} right, {secs / len(rows):.1f} s per question")
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
