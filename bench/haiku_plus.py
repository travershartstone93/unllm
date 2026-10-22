#!/usr/bin/env python3
"""Claude Haiku reading unllm's paragraphs: the same test as bench/groq_plus.py
variant A (question + the 12 best vector-search paragraphs, 400 characters
each, answer plus the paragraph number used, 0 = own memory), one question per
`claude -p --model haiku` call, on the first N of the 400 pilot questions so
Haiku alone, Groq alone and Groq + unllm exist for the same questions.

  haiku_plus.py [N]      default 50

Pass mark (written before the run): beat Haiku alone on the same N by at
least 4 for N = 50 (scaled otherwise). Scored right out of N; also the
fixed / broken split against Haiku alone. Resumable: DATA/vec/haikuplus<N>.jsonl.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import readers  # noqa: E402
from gold import haiku  # noqa: E402
from groq_plus import K, SYSTEM, VEC, parse  # noqa: E402
from qa_score import nq_right  # noqa: E402
from taskprogress import Task  # noqa: E402


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 50
    mark_gain = round(4 * n / 50)
    items = [json.loads(l) for l in open(f"{VEC}/testset.jsonl")][:n]
    vec = {json.loads(l)["id"]: json.loads(l)["passages"] for l in open(f"{VEC}/vector.jsonl")}
    hk = {json.loads(l)["id"]: json.loads(l) for l in open(f"{VEC}/haiku-memory.jsonl")}
    gq = {json.loads(l)["id"]: json.loads(l) for l in open(f"{VEC}/groq-memory.jsonl")}
    gp_path = f"{VEC}/groqplus400-A.jsonl"
    gp = {json.loads(l)["id"]: json.loads(l) for l in open(gp_path)} if os.path.exists(gp_path) else {}
    path = f"{VEC}/haikuplus{n}.jsonl"
    done = {json.loads(l)["id"]: json.loads(l) for l in open(path)} if os.path.exists(path) else {}
    bar = Task("haiku-plus", f"Haiku + unllm on {n} questions", total=n)
    bar.step(len(done))
    with open(path, "a") as f:
        for it in items:
            if it["id"] in done:
                continue
            ps = [" ".join(p.split())[:400] for p in readers.one_per_article(vec[it["id"]])[:K]]
            text = (SYSTEM + " Do not use any tools.\n\nQuestion: " + it["question"] + "\n\nParagraphs:\n"
                    + "\n\n".join(f"[{k}] {p}" for k, p in enumerate(ps, 1)))
            got = parse(haiku(text))
            ans = got["answer"] if got else None
            num = got["passage"] if got else 0
            row = {"id": it["id"], "answer": ans, "passage": num,
                   "right": bool(ans and nq_right(ans, it["aliases"])),
                   "cited_ok": bool(0 < num <= len(ps) and nq_right(ps[num - 1], it["aliases"])),
                   "found": any(nq_right(p, it["aliases"]) for p in ps)}
            done[it["id"]] = row
            f.write(json.dumps(row) + "\n")
            f.flush()
            bar.step()
    bar.finish()
    rows = [done[it["id"]] for it in items if it["id"] in done]
    ids = [r["id"] for r in rows]
    right = sum(r["right"] for r in rows)
    alone = sum(hk[i]["right"] for i in ids)
    fixed = sum(1 for r in rows if r["right"] and not hk[r["id"]]["right"])
    broken = sum(1 for r in rows if hk[r["id"]]["right"] and not r["right"])
    missing = sum(1 for r in rows if not r["answer"])
    used = sum(1 for r in rows if r["passage"] > 0)
    lines = [f"\n### Haiku + unllm paragraphs on the first {len(rows)} pilot questions (2026-10-19)",
             f"- Haiku + unllm: right {right} of {len(rows)}; Haiku alone on the same {len(rows)}: {alone}; "
             f"fixed {fixed}, broken {broken}; {missing} unanswered; "
             f"{'PASS' if right - alone >= mark_gain else 'FAIL'} (mark: alone + {mark_gain})",
             f"- answer was in the {K} paragraphs {sum(r['found'] for r in rows)} times; used a paragraph {used} times, "
             f"of which {sum(r['cited_ok'] for r in rows)} really held the answer",
             f"- same questions: Groq alone {sum(gq[i]['right'] for i in ids)}; Groq + unllm "
             f"{sum(gp[i]['right'] for i in ids if i in gp)} of {sum(1 for i in ids if i in gp)}"]
    text = "\n".join(lines)
    print(text)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md"), "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
