#!/usr/bin/env python3
"""Claude Haiku answering the 400 vector-pilot questions from memory, batched
15 to a call like the Round 1 labelling, so it sits on the same table as
Groq's qwen3.8-27b (bench/groq_read.py memory).

  haiku_memory.py

Scored right out of 400 against the answer keys (whole-word match); a missing
or refused answer is wrong. Resumable: DATA/vec/haiku-memory.jsonl.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from gold import haiku, parse_list  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from taskprogress import Task  # noqa: E402

VEC = f"{DATA}/vec"
BATCH = 15
HEAD = ("Answer each question from your own knowledge with the shortest correct answer (a name, date, place or "
        "number). Do not use any tools. Always give your best answer; never say you don't know.\n")


def main():
    items = [json.loads(l) for l in open(f"{VEC}/testset.jsonl")]
    path = f"{VEC}/haiku-memory.jsonl"
    done = {json.loads(l)["id"] for l in open(path)} if os.path.exists(path) else set()
    todo = [it for it in items if it["id"] not in done]
    bar = Task("haiku-memory", "Haiku from memory: 400 pilot questions", total=len(items))
    bar.step(len(done))
    with open(path, "a") as f:
        for k in range(0, len(todo), BATCH):
            batch = todo[k:k + BATCH]
            text = HEAD + "".join(f"\n=== ITEM {it['id']} ===\n{it['question']}\n" for it in batch)
            text += '\nReply with only a JSON array, one object per item: [{"id": "...", "answer": "..."}]'
            got = {str(o.get("id")): o.get("answer") for o in parse_list(haiku(text)) if isinstance(o, dict)}
            for it in batch:
                a = got.get(it["id"])
                a = a.strip() if isinstance(a, str) else None
                f.write(json.dumps({"id": it["id"], "answer": a, "right": bool(a and nq_right(a, it["aliases"]))}) + "\n")
            f.flush()
            bar.step(len(batch))
    bar.finish()
    rows = [json.loads(l) for l in open(path)]
    right = sum(r["right"] for r in rows)
    missing = sum(1 for r in rows if not r["answer"])
    line = f"Haiku from memory: right {right} of {len(rows)} ({100 * right / len(rows):.0f}%), {missing} unanswered"
    print(line)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md"), "a") as f:
        f.write("\n### " + line + " (2026-10-19)\n")


if __name__ == "__main__":
    main()
