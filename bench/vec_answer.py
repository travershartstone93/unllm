#!/usr/bin/env python3
"""Does better search turn into more right answers? The vector pilot's word
and vector passage lists (DATA/vec/word.jsonl, vector.jsonl) each go through
the same answer step: DeBERTa reader plus Qwen3 reranker, span chosen by
span probability times reranker probability (the MIX rule from stack.py).

  vec_answer.py

Variants: word (unllm's 20), vector (the 20 closest by meaning), both (the
two lists merged, reranker keeps 20). Scored right out of all against the
answer keys, two ways: "top" always answers with the chosen span; "gated"
answers only when unllm's vote gate passes (no fallback, so a gated miss is
a wrong answer). Also reports "found" (answer in the list) for reference.
Writes DATA/vec/answers.jsonl and appends the table to the pilot report.
"""

import json
import os
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import readers  # noqa: E402
import rerank  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from stack import passes, pick  # noqa: E402
from taskprogress import Task  # noqa: E402

VEC = f"{DATA}/vec"
REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md")


def load(path):
    return {json.loads(l)["id"]: json.loads(l) for l in open(path)}


def main():
    items = load(f"{VEC}/testset.jsonl")
    word = {i: r["passages"] for i, r in load(f"{VEC}/word.jsonl").items()}
    vec = {i: r["passages"] for i, r in load(f"{VEC}/vector.jsonl").items()}
    ids = [i for i in items if i in word and i in vec]
    reader = readers.Reader(readers.MODELS["deberta"])
    rr = rerank.load("qwen3-0.6b")
    names = ("word", "vector", "both")
    tot = {n: {"found": 0, "top": 0, "gated": 0} for n in names}
    bar = Task("vec-answer", "Vector pilot: answers from each search", total=len(ids))
    with open(f"{VEC}/answers.jsonl", "w") as out:
        for i in ids:
            it = items[i]
            q, al = it["question"], it["aliases"]
            lists = {"word": readers.one_per_article(word[i]), "vector": readers.one_per_article(vec[i])}
            merged = readers.one_per_article(word[i] + [p for p in vec[i] if p not in word[i]])
            if len(merged) > 20:
                s = rr.scores(q, merged)
                merged = [p for _, p in sorted(zip(s, merged), key=lambda x: -x[0])[:20]]
            lists["both"] = merged
            row = {"id": i}
            for n in names:
                ps = lists[n]
                found = any(nq_right(p, al) for p in ps)
                spans = reader.read(q, ps) if ps else []
                rel = rr.scores(q, ps) if ps else []
                for s in spans:
                    s["rel"] = rel[s["passage"]]
                best = pick(spans, lambda s: s["prob"] * s["rel"])
                top = bool(best and nq_right(best["text"], al))
                gated = bool(best and passes(best, spans) and nq_right(best["text"], al))
                row[n] = {"found": found, "top": top, "gated": gated, "answer": best["text"] if best else None}
                tot[n]["found"] += found
                tot[n]["top"] += top
                tot[n]["gated"] += gated
            out.write(json.dumps(row) + "\n")
            bar.step()
    bar.finish()
    n = len(ids)
    lines = [f"| passages from | answer in list | right, always answer | right, gated | (n={n}) |", "|---|---|---|---|---|"]
    for name in names:
        t = tot[name]
        lines.append(f"| {name} | {t['found']} ({100 * t['found'] / n:.0f}%) | {t['top']} ({100 * t['top'] / n:.0f}%) | "
                     f"{t['gated']} ({100 * t['gated'] / n:.0f}%) | |")
    text = "\n".join(lines)
    print(text)
    with open(REPORT, "a") as f:
        f.write("\n### Result\n" + text + "\n")
    del reader, rr
    torch.cuda.empty_cache()


if __name__ == "__main__":
    main()
