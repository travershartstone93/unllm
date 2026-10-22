#!/usr/bin/env python3
"""The corrected single experiment (2026-10-19): meaning search -> reranker -> reader. Same 50
pilot questions (seed 7) and the library's own top-20 paragraphs as reader50.py. The Qwen3
reranker scores each paragraph against the question and reorders the 20; the DeBERTa reader
reads the top-scored paragraph. Marks, written first: holder first >= 28 of the 39 that have
one (from 19); reader's answer right >= 24 of 50 (rules 16 + 8). Both must pass.
"""

import json
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import readers  # noqa: E402
import rerank  # noqa: E402
import vec_pilot  # noqa: E402
from qa_score import nq_right  # noqa: E402
from spine_audit import AUDIT, REPORT  # noqa: E402

TOOL = os.path.expanduser("~/repo-work/unllm/build/unllm_meaning")
INDEX = os.path.expanduser("~/.local/share/unllm/vectors/wikipedia-simple.unlpq")
MODEL = os.path.expanduser("~/.local/share/unllm/models/bge-small-en-v1.5")


def main():
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    random.seed(7)
    sample = random.sample(items, 50)
    out = subprocess.run([TOOL, INDEX, MODEL, "20", "64"], input="".join(it["question"] + "\n" for it in sample),
                         capture_output=True, text=True).stdout
    ids = [json.loads(l)["ids"] for l in out.splitlines() if l.startswith("{")]
    con = vec_pilot.shard()
    ranker = rerank.load("qwen3-0.6b")
    reader = readers.Reader(readers.MODELS["deberta"])
    with_holder = first_before = first_after = right = 0
    rows = []
    for it, idl in zip(sample, ids):
        ps = []
        for i in idl:
            row = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid = ?",
                              (i & 0xffffffff,)).fetchone()
            if row:
                ps.append(f"{row[0]}: {' '.join(row[1].split())}")
        holders = [k for k, p in enumerate(ps) if nq_right(p, it["aliases"])]
        sc = ranker.scores(it["question"], ps)
        order = sorted(range(len(ps)), key=lambda k: -float(sc[k]))
        top = ps[order[0]] if order else ""
        spans = reader.read(it["question"], [top]) if top else []
        ans = spans[0]["text"] if spans else ""
        ok = bool(ans) and nq_right(ans, it["aliases"])
        with_holder += bool(holders)
        first_before += bool(holders) and holders[0] == 0
        first_after += bool(holders) and order[0] in holders
        right += ok
        rows.append({"id": it["id"], "question": it["question"], "key": it["aliases"][0], "holder_before": holders[0] if holders else -1,
                     "top_after": order[0] if order else -1, "top_holds": bool(holders) and order[0] in holders, "answer": ans, "right": ok})
    with open(f"{AUDIT}/reader50b.jsonl", "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    l1 = f"holder first: {first_before} of {with_holder} before the reranker, {first_after} after; {'PASS' if first_after >= 28 else 'FAIL'} (mark 28)"
    l2 = f"reader on the reranked top paragraph: right {right} of 50 (rules 16); {'PASS' if right >= 24 else 'FAIL'} (mark 24)"
    print(l1)
    print(l2)
    with open(REPORT, "a") as f:
        f.write("\n### Meaning search -> reranker -> reader, same 50 (2026-10-19)\n- " + l1 + "\n- " + l2 + "\n")


if __name__ == "__main__":
    main()
