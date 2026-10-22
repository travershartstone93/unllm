#!/usr/bin/env python3
"""The one experiment (2026-10-19): rules out, trained reader in. 50 pilot questions drawn at
random (seed 7); both arms get the library's own meaning-search top 20 (all passage kinds).
Arm A: the rules' answers (run meaning3). Arm B: DeBERTa-v3 SQuAD2's best span across the 20
by score margin, no gate, no refusal. Mark, written first: B - A >= 8.
"""

import json
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import readers  # noqa: E402
import vec_pilot  # noqa: E402
from qa_score import nq_right  # noqa: E402
from spine_audit import AUDIT, REPORT  # noqa: E402

TOOL = os.path.expanduser("~/repo-work/unllm/build/unllm_meaning")
INDEX = os.path.expanduser("~/.local/share/unllm/vectors/wikipedia-simple.unlpq")
MODEL = os.path.expanduser("~/.local/share/unllm/models/bge-small-en-v1.5")


def main():
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    rules = {r["id"]: r for r in vec_pilot.load(f"{AUDIT}/meaning3.jsonl")}
    random.seed(7)
    sample = random.sample(items, 50)
    out = subprocess.run([TOOL, INDEX, MODEL, "20", "64"], input="".join(it["question"] + "\n" for it in sample),
                         capture_output=True, text=True).stdout
    ids = [json.loads(l)["ids"] for l in out.splitlines() if l.startswith("{")]
    con = vec_pilot.shard()
    reader = readers.Reader(readers.MODELS["deberta"])
    a = b = 0
    rows = []
    for it, idl in zip(sample, ids):
        ps = []
        for i in idl:
            row = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid = ?",
                              (i & 0xffffffff,)).fetchone()
            if row:
                ps.append(f"{row[0]}: {' '.join(row[1].split())}")
        spans = reader.read(it["question"], ps)
        best = max(spans, key=lambda s: s["margin"]) if spans else None
        ans_b = best["text"] if best else ""
        ans_a = rules.get(it["id"], {}).get("answer", "")
        ok_a = bool(ans_a) and nq_right(ans_a, it["aliases"])
        ok_b = bool(ans_b) and nq_right(ans_b, it["aliases"])
        a += ok_a
        b += ok_b
        rows.append({"id": it["id"], "question": it["question"], "key": it["aliases"][0], "rules": ans_a[:120], "rules_right": ok_a,
                     "reader": ans_b, "reader_right": ok_b, "reader_passage": best["passage"] if best else -1})
    with open(f"{AUDIT}/reader50.jsonl", "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    line = (f"50 questions, same 20 paragraphs from the library's meaning search: rules (A) {a}, DeBERTa reader best span (B) {b}; "
            f"B - A = {b - a}; {'PASS' if b - a >= 8 else 'FAIL'} (mark B - A >= 8)")
    print(line)
    with open(REPORT, "a") as f:
        f.write("\n### Rules out, reader in: the one experiment (2026-10-19)\n- " + line + "\n")


if __name__ == "__main__":
    main()
