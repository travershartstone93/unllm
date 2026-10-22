#!/usr/bin/env python3
"""The rule-free arm (2026-10-19): DPR reader (facebook/dpr-reader-single-nq-base, trained on
Natural Questions) on the same 50 questions and the same 20 library meaning-search paragraphs
as reader50.py. Its selection head scores every passage for HOLDING the answer (not for being
on topic) and its span head extracts, all in one pass, no hand-written rules in the path.
Mark, written first: right >= 24 of 50 (rules 16, SQuAD reader 10, reranker+reader 15).
"""

import json
import os
import random
import subprocess
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vec_pilot  # noqa: E402
from qa_score import nq_right  # noqa: E402
from spine_audit import AUDIT, REPORT  # noqa: E402

TOOL = os.path.expanduser("~/repo-work/unllm/build/unllm_meaning")
INDEX = os.path.expanduser("~/.local/share/unllm/vectors/wikipedia-simple.unlpq")
MODEL = os.path.expanduser("~/.local/share/unllm/models/bge-small-en-v1.5")
DPR = os.path.expanduser("~/Downloads/unllm-models/hf/dpr-reader-single-nq-base")
DEV = torch.device("cuda" if torch.cuda.is_available() else "cpu")


def main():
    from transformers import DPRReader, DPRReaderTokenizerFast
    tok = DPRReaderTokenizerFast.from_pretrained(DPR)
    model = DPRReader.from_pretrained(DPR).to(DEV).eval()
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    random.seed(7)
    sample = random.sample(items, 50)
    out = subprocess.run([TOOL, INDEX, MODEL, "20", "64"], input="".join(it["question"] + "\n" for it in sample),
                         capture_output=True, text=True).stdout
    ids = [json.loads(l)["ids"] for l in out.splitlines() if l.startswith("{")]
    con = vec_pilot.shard()
    right = with_holder = picked_holding = right_when_picked = 0
    rows = []
    for it, idl in zip(sample, ids):
        titles, texts, full = [], [], []
        for i in idl:
            row = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid = ?",
                              (i & 0xffffffff,)).fetchone()
            if row:
                titles.append(row[0])
                texts.append(" ".join(row[1].split()))
                full.append(f"{row[0]}: {' '.join(row[1].split())}")
        holders = [k for k, p in enumerate(full) if nq_right(p, it["aliases"])]
        with_holder += bool(holders)
        enc = tok(questions=it["question"], titles=titles, texts=texts, return_tensors="pt",
                  padding=True, truncation=True, max_length=350)
        with torch.no_grad():
            res = model(**{k: v.to(DEV) for k, v in enc.items()})
        spans = tok.decode_best_spans(enc, type(res)(start_logits=res.start_logits.cpu(), end_logits=res.end_logits.cpu(),
                                                     relevance_logits=res.relevance_logits.cpu()),
                                      num_spans=1, num_spans_per_passage=1, max_answer_length=10)
        best = spans[0] if spans else None
        ans = best.text if best else ""
        picked = int(best.doc_id) if best else -1
        ok = bool(ans) and nq_right(ans, it["aliases"])
        right += ok
        if holders and picked in holders:
            picked_holding += 1
            right_when_picked += ok
        rows.append({"id": it["id"], "question": it["question"], "key": it["aliases"][0], "answer": ans,
                     "picked": picked, "picked_title": titles[picked] if 0 <= picked < len(titles) else "",
                     "picked_holds": picked in holders, "holder_at": holders[0] if holders else -1, "right": ok})
    with open(f"{AUDIT}/dpr50.jsonl", "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    l1 = (f"DPR reader, pick and extract across the 20, no rules: right {right} of 50; "
          f"{'PASS' if right >= 24 else 'FAIL'} (mark 24; rules 16, SQuAD reader 10, reranker+reader 15)")
    l2 = (f"of the {with_holder} questions with a holding paragraph in the 20, DPR picked a holding one {picked_holding} times "
          f"and was right {right_when_picked} of those")
    print(l1)
    print(l2)
    with open(REPORT, "a") as f:
        f.write("\n### DPR reader, the rule-free arm, same 50 (2026-10-19)\n- " + l1 + "\n- " + l2 + "\n")


if __name__ == "__main__":
    main()
