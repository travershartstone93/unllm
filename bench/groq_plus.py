#!/usr/bin/env python3
"""Groq plus unllm: unllm's vector search finds the paragraphs, Groq's
qwen3.8-27b reads them. On the 58 questions already answered from memory
(DATA/vec/compare58.jsonl), so the result sits next to Groq alone, the local
4B and unllm alone.

  groq_plus.py [A|B] [--all]   default: both variants on the 58; --all runs
                               the full 400 (pass mark: Groq alone + 20)

Variant A: question + the 12 best vector-search paragraphs (400 characters
each). Variant B: the same plus unllm's own answer offered as the library's
suggestion. Groq returns the answer and the paragraph number it used (0 =
own memory), so every library answer carries a source. Scored right out of
all; also how often a paragraph was used and how often a cited paragraph
really held the answer, and the score with a mechanical citation check (keep
the cited answer only if its paragraph contains it, else Groq's memory
answer from groq-memory.jsonl). Resumable: DATA/vec/groqplus[400]-<v>.jsonl.
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import readers  # noqa: E402
from groq_read import api_key, call  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from taskprogress import Task  # noqa: E402

VEC = f"{DATA}/vec"
K = 12
SYSTEM = ("You are given a question and numbered encyclopedia paragraphs found by a search. If a paragraph holds "
          "the answer, take it from there and give that paragraph's number. Otherwise answer from your own "
          "knowledge and give 0. Give the shortest correct answer (a name, date, place or number). Always give "
          "your best answer; never say you don't know. Reply with only JSON: {\"answer\": \"...\", \"passage\": N}")


def parse(text):
    for cand in reversed(re.findall(r"\{[^{}]*\"answer\"[^{}]*\}", text or "", re.S)):
        try:
            o = json.loads(cand)
            return {"answer": str(o.get("answer", "")).strip(), "passage": int(o.get("passage", 0) or 0)}
        except (ValueError, TypeError):
            continue
    return None


def contains(ans, text):
    return bool(ans) and re.search(r"(?<!\w)" + re.escape(ans.lower()) + r"(?!\w)", text.lower()) is not None


def main():
    use_all = "--all" in sys.argv
    variants = [a for a in sys.argv[1:] if a in ("A", "B")] or ["A", "B"]
    key = api_key()
    items = {json.loads(l)["id"]: json.loads(l) for l in open(f"{VEC}/testset.jsonl")}
    vec = {json.loads(l)["id"]: json.loads(l)["passages"] for l in open(f"{VEC}/vector.jsonl")}
    memory = {json.loads(l)["id"]: json.loads(l) for l in open(f"{VEC}/groq-memory.jsonl")}
    if use_all:
        word = {json.loads(l)["id"]: json.loads(l) for l in open(f"{VEC}/word.jsonl")}
        base = [{"id": i, "unllm_all": word.get(i, {}).get("answer", "")} for i in items]
        tag, n = "groqplus400", len(base)
        mark = sum(r["right"] for r in memory.values()) + 20
    else:
        base = [json.loads(l) for l in open(f"{VEC}/compare58.jsonl")]
        tag, n, mark = "groqplus", 58, 32
    bar = Task("groq-plus", f"Groq + unllm on {n} questions", total=len(variants) * len(base))
    lines = [f"\n### Groq + unllm ({n} questions; Groq alone {sum(memory[b['id']]['right'] for b in base if b['id'] in memory)}; pass mark {mark})"]
    for v in variants:
        path = f"{VEC}/{tag}-{v}.jsonl"
        done = {json.loads(l)["id"]: json.loads(l) for l in open(path)} if os.path.exists(path) else {}
        bar.skip(len(done))
        with open(path, "a") as f:
            for b in base:
                if b["id"] in done:
                    continue
                it = items[b["id"]]
                ps = [" ".join(p.split())[:400] for p in readers.one_per_article(vec[b["id"]])[:K]]
                user = f"Question: {it['question']}\n\nParagraphs:\n" + "\n\n".join(f"[{k}] {p}" for k, p in enumerate(ps, 1))
                if v == "B":
                    user += f"\n\nThe library's own suggested answer (may be wrong): {b['unllm_all'][:200]}"
                got, err = call(key, SYSTEM, user, parse)
                ans = got["answer"] if got else None
                n = got["passage"] if got else 0
                row = {"id": b["id"], "answer": ans, "passage": n, "error": err,
                       "right": bool(ans and nq_right(ans, it["aliases"])),
                       "cited_ok": bool(0 < n <= len(ps) and nq_right(ps[n - 1], it["aliases"])),
                       "found": any(nq_right(p, it["aliases"]) for p in ps)}
                done[b["id"]] = row
                f.write(json.dumps(row) + "\n")
                f.flush()
                bar.step(detail=v)
        rows = list(done.values())
        right = sum(r["right"] for r in rows)
        used = sum(1 for r in rows if r["passage"] > 0)
        cited_ok = sum(r["cited_ok"] for r in rows)
        found = sum(r["found"] for r in rows)
        errs = sum(1 for r in rows if r["error"])
        # Mechanical citation check: a cited answer counts only if its paragraph contains it; else memory.
        checked = 0
        for r in rows:
            it = items[r["id"]]
            ps = [" ".join(p.split())[:400] for p in readers.one_per_article(vec[r["id"]])[:K]]
            nn, ans = r["passage"], r["answer"] or ""
            final = ans if 0 < nn <= len(ps) and contains(ans, ps[nn - 1]) else (memory.get(r["id"], {}).get("answer") or ans)
            checked += bool(final and nq_right(final, it["aliases"]))
        lines.append(f"- variant {v}: right {right} of {len(rows)}; with citation check {checked}; answer was in the "
                     f"{K} paragraphs {found} times; used a paragraph {used} times, of which {cited_ok} really held the "
                     f"answer; {errs} failed requests; {'PASS' if right >= mark else 'FAIL'} (mark {mark})")
    bar.finish()
    text = "\n".join(lines)
    print(text)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md"), "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
