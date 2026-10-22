#!/usr/bin/env python3
"""Does a reranker in front of the reader earn its place? Reader alone against
reader plus reranker, on unllm's cached passages, scored against the answer keys.

  stack.py

Versions (DeBERTa-v3 reader, Qwen3-Reranker-0.6B):
  R    the reader reads every passage (one per article, up to 20)
  R5   the reranker keeps its top 5 passages; the reader reads those
  MIX  the reader reads every passage; the span with the highest span
       probability times reranker probability wins (fixed rule, nothing tuned)
Each version is scored two ways, right out of all: "top" always answers with
its span; "gated" uses the span only when unllm's vote gate passes (2+ agreeing
articles or probability >= 0.9) and otherwise falls back to unllm's own answer.
Saves DATA/stack/<set>.jsonl and prints the table; tray bar.
"""

import json
import os
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import readers  # noqa: E402
import rerank  # noqa: E402
from evaluate import judge  # noqa: E402
from jobs import DATA  # noqa: E402
from taskprogress import Task  # noqa: E402


def pick(spans, key):
    good = [s for s in spans if s["text"]]
    return max(good, key=key) if good else None


def passes(best, pool):
    """unllm's vote gate applied to the chosen span: margin > 0 and (2+ agreeing articles or prob >= 0.9)."""
    if not best or best["margin"] <= 0:
        return False
    k = readers.keys(best["text"])
    votes = sum(1 for s in pool if s["margin"] > 0 and s["text"] and k and readers.keys(s["text"])
                and (k <= readers.keys(s["text"]) or readers.keys(s["text"]) <= k))
    return votes >= 2 or best["prob"] >= 0.9


def main():
    data = readers.sets()
    reader = readers.Reader(readers.MODELS["deberta"])
    rr = rerank.load("qwen3-0.6b")
    os.makedirs(f"{DATA}/stack", exist_ok=True)
    bar = Task("stack", "Reader vs reader + reranker", total=sum(len(v[0]) for v in data.values()))
    table = []
    for set_name, (items, cache) in data.items():
        tot = {k: 0 for k in ("unllm", "R top", "R gated", "R5 top", "R5 gated", "MIX top", "MIX gated")}
        n = 0
        with open(f"{DATA}/stack/{set_name}.jsonl", "w") as f:
            for it in items:
                reply = cache.get(it["question"])
                bar.step(detail=set_name)
                if reply is None:
                    continue
                n += 1
                u = bool(judge(it, reply)[1])
                row = {"id": it["id"], "unllm": u}
                ps = readers.one_per_article(reply["passages"])
                spans = reader.read(it["question"], ps) if ps else []
                rel = rr.scores(it["question"], ps) if ps else []
                for s in spans:
                    s["rel"] = rel[s["passage"]]
                top5 = set(sorted(range(len(ps)), key=lambda i: -rel[i])[:5])
                versions = {
                    "R": (spans, lambda s: s["margin"]),
                    "R5": ([s for s in spans if s["passage"] in top5], lambda s: s["margin"]),
                    "MIX": (spans, lambda s: s["prob"] * s["rel"]),
                }
                for v, (pool, key) in versions.items():
                    best = pick(pool, key)
                    row[f"{v} top"] = bool(best and readers.right(it, best["text"]))
                    row[f"{v} gated"] = bool(readers.right(it, best["text"])) if passes(best, pool) else u
                for k in tot:
                    tot[k] += row[k]
                f.write(json.dumps(row) + "\n")
        table.append((set_name, n, tot))
    bar.finish()
    lines = ["| set | n | " + " | ".join(table[0][2]) + " |", "|---|---|" + "---|" * len(table[0][2])]
    for set_name, n, tot in table:
        lines.append(f"| {set_name} | {n} | " + " | ".join(str(v) for v in tot.values()) + " |")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
