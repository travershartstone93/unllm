#!/usr/bin/env python3
"""Retrieval-only measurement on the verified 50 (DATA/audit/dev50.tsv).

  retrieval50.py run NAME [LAYERS] the library CLI's ranked list (top 50) per question
  retrieval50.py score NAME [NAME] article and answer-passage ranks, recall@1/5/10, MRR

A run file DATA/audit/retr-NAME.jsonl holds {id, passages: ["Title: text", ...]} in rank order,
so any search (CLI or prototype) can be scored the same way. The answer step is not involved.
Correct article: title equal (title_key) to the verified source_title. Answer-bearing passage:
the verified answer pattern matches its text."""

import json
import os
import re
import subprocess
import sys

AUDIT = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
SET = os.environ.get("RETR_SET", f"{AUDIT}/dev50.tsv")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
OLD = "kinds,fact,super,roman,phrase,named,howmany,howto,of,rerank,leadfirst,attr,qa,defs,floor,union,floorwide,song"
REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md")


def key(s):
    t = re.sub(r"[^0-9a-z]+", " ", s.lower()).strip()
    for a in ("the ", "a ", "an "):
        if t.startswith(a):
            return t[len(a):]
    return t


def questions():
    return [dict(zip(("id", "question", "pattern", "type", "source"), l.split("\t")))
            for l in open(SET).read().splitlines()[1:]]


def run(name, layers=OLD, extra=None):
    out = f"{AUDIT}/retr-{name}.jsonl"
    with open(out, "w") as f:
        for q in questions():
            env = dict(os.environ, UNLLM_LAYERS=layers, UNLLM_TOP="50")
            env.update(extra or {})
            r = subprocess.run([CLI, "--passages", "50", q["question"]], capture_output=True, text=True,
                               errors="replace", env=env, timeout=180)
            ps = [l[10:] for l in r.stdout.splitlines() if l.startswith("[passage] ")]
            f.write(json.dumps({"id": q["id"], "passages": ps[:50]}) + "\n")
    print(f"saved {out}")


def ranks(q, ps):
    art, seen = -1, []
    for p in ps:
        t = p.split(": ", 1)[0]
        if t not in seen:
            seen.append(t)
            if key(t) == key(q["source"]) and art < 0:
                art = len(seen)
    ans = next((k + 1 for k, p in enumerate(ps) if re.search(q["pattern"], p, re.I)), -1)
    return art, ans


def summary(rs):
    n = len(rs)
    r = lambda k: sum(1 for x in rs if 0 < x <= k)  # noqa: E731
    mrr = sum(1 / x for x in rs if x > 0) / n
    return f"R@1 {r(1)}, R@5 {r(5)}, R@10 {r(10)}, MRR {mrr:.2f}"


def score(names):
    qs = questions()
    lines = ["", "| run | article (by source title) | first answer-bearing passage |", "|---|---|---|"]
    for name in names:
        rows = {json.loads(l)["id"]: json.loads(l)["passages"] for l in open(f"{AUDIT}/retr-{name}.jsonl")}
        arts, anss = [], []
        for q in qs:
            a, b = ranks(q, rows.get(q["id"], []))
            arts.append(a)
            anss.append(b)
        lines.append(f"| {name} | {summary(arts)} | {summary(anss)} |")
    text = "\n".join(lines)
    print(text)
    with open(REPORT, "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "run":
        run(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else OLD)
    elif len(sys.argv) >= 3 and sys.argv[1] == "score":
        score(sys.argv[2:])
    else:
        sys.exit(__doc__)
