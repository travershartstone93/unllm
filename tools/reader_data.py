#!/usr/bin/env python3
"""Training data for the reader: NQ-open training questions, each with the
passages unllm itself finds for it (`unllm --passages 20`), so the reader
learns on what it will really see, wrong articles included.

  reader_data.py OUT.jsonl [--n 10000] [--seed 5] [--workers 3]

A passage holding a gold answer (whole words) is a positive, with the
first match as its span; the others are negatives, which teach the reader
to say "no answer here". Questions that also appear in NQ-open validation
are never used. Resumable: questions already in OUT are skipped. Shows a
progress bar in the system tray and pauses above 85 C.
"""

import argparse
import concurrent.futures
import json
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qa_score import normalize, run_unllm  # noqa: E402
from recall import REPO, cool_down  # noqa: E402
from taskprogress import Task  # noqa: E402

NQ = os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open")


def find_span(text, answers):
    """The first whole-word, case-blind match of the longest answer: (start, text)."""
    for a in sorted(answers, key=len, reverse=True):
        a = a.strip()
        if not a:
            continue
        m = re.search(r"(?<!\w)" + re.escape(a) + r"(?!\w)", text, re.I)
        if m:
            return m.start(), m.group(0)
    return None


def main():
    import pyarrow.parquet as pq
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--n", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=5)
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--unllm", default=os.path.join(REPO, "build", "unllm"))
    args = ap.parse_args()
    held = {normalize(q) for q in pq.read_table(os.path.join(NQ, "validation.parquet")).column("question").to_pylist()}
    rows = [r for r in pq.read_table(os.path.join(NQ, "train.parquet")).to_pylist() if normalize(r["question"]) not in held]
    rows = random.Random(args.seed).sample(rows, args.n)
    done = set()
    if os.path.exists(args.out):
        with open(args.out) as f:
            done = {json.loads(line)["question"] for line in f}
    todo = [r for r in rows if r["question"] not in done]
    bar = Task("reader-data", "Reader training passages", total=len(rows))
    bar.step(len(rows) - len(todo))

    def one(r):
        cool_down()
        # Plain rules: the passages, not an answer, are what is wanted.
        reply = run_unllm(args.unllm, r["question"])
        ps = []
        for p in reply["passages"][:20]:
            hit = find_span(p, r["answer"])
            ps.append({"context": p, "start": hit[0] if hit else -1, "text": hit[1] if hit else ""})
        return {"question": r["question"], "answers": r["answer"], "passages": ps}

    with open(args.out, "a") as f, concurrent.futures.ThreadPoolExecutor(args.workers) as ex:
        for rec in ex.map(one, todo):
            f.write(json.dumps(rec) + "\n")
            f.flush()
            bar.step(detail=rec["question"][:60])
    bar.finish()


if __name__ == "__main__":
    sys.exit(main())
