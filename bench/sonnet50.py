#!/usr/bin/env python3
"""Nuke-it arm (2026-10-19): Claude Sonnet in the middle, unllm's own retrieval underneath.
Same seed-7 50 questions, the library's 20 meaning-search paragraphs each. Sonnet answers
from a paragraph (citing its number) or from its own knowledge (0). Goal set by the user:
iterate until 50 of 50, documenting every change per attempt; then scale back from the win.

  sonnet50.py run N [--model sonnet]   attempt N -> DATA/audit/sonnet50-aN.jsonl (resumable)
  sonnet50.py score N                  score + full miss table
Key adjudications live in DATA/audit/keyfixes.jsonl ({id, alias, why}); scores are printed
both raw and with adjudications, and every adjudication is listed in the report.
"""

import json
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from spine_audit import AUDIT  # noqa: E402
from taskprogress import Task  # noqa: E402

TOOL = os.path.expanduser("~/repo-work/unllm/build/unllm_meaning")
INDEX = os.path.expanduser("~/.local/share/unllm/vectors/wikipedia-simple.unlpq")
MODEL = os.path.expanduser("~/.local/share/unllm/models/bge-small-en-v1.5")
PROMPT = """Do not use any tools. You are answering one quiz question, using encyclopedia paragraphs found by a search.

Question: {q}

Paragraphs:
{ps}

These quiz questions were written in early 2018: answer for that time (officeholders, teams, shows and "latest" events as of early 2018, not today). If the question's subject is ambiguous, it means the most famous one. If a paragraph states the answer, take the answer from it and give that paragraph's number; but if the paragraphs are about a different subject than the question means, ignore them and use your own knowledge, giving 0. Give the shortest exact answer: a name, a date (the full date if known, else the year), a number or a place, in the form the question asks for (a "when was the last time" sports question wants the season or year). Never say you don't know; give your best answer.
Reply with only JSON: {{"answer": "...", "passage": N}}"""


def sample50():
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    random.seed(7)
    return random.sample(items, 50)


def paragraphs(sample):
    out = subprocess.run([TOOL, INDEX, MODEL, "20", "64"], input="".join(it["question"] + "\n" for it in sample),
                         capture_output=True, text=True).stdout
    ids = [json.loads(l)["ids"] for l in out.splitlines() if l.startswith("{")]
    con = vec_pilot.shard()
    per = []
    for idl in ids:
        ps = []
        for i in idl:
            row = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid = ?",
                              (i & 0xffffffff,)).fetchone()
            if row:
                ps.append(f"{row[0]}: {' '.join(row[1].split())[:700]}")
        per.append(ps)
    return per


def fixes():
    path = f"{AUDIT}/keyfixes.jsonl"
    out = {}
    if os.path.exists(path):
        for l in open(path):
            r = json.loads(l)
            out.setdefault(r["id"], []).append(r["alias"])
    return out


def call(model, prompt):
    r = subprocess.run(["claude", "-p", "--model", model, prompt], capture_output=True, text=True,
                       timeout=600, cwd=os.path.join(DATA, "gold"), stdin=subprocess.DEVNULL)
    text = r.stdout
    at = text.rfind('{"answer"')
    if at < 0:
        at = text.rfind("{")
    try:
        o = json.loads(text[at:text.index("}", at) + 1]) if at >= 0 else {}
    except ValueError:
        o = {}
    return str(o.get("answer", "")).strip(), int(o.get("passage") or 0)


def run(n, model):
    sample = sample50()
    per = paragraphs(sample)
    path = f"{AUDIT}/sonnet50-a{n}.jsonl"
    done = {json.loads(l)["id"] for l in open(path)} if os.path.exists(path) else set()
    bar = Task("sonnet50", f"Sonnet + unllm, attempt {n}", total=len(sample))
    bar.step(len(done))
    with open(path, "a") as f:
        for it, ps in zip(sample, per):
            if it["id"] in done:
                continue
            ans, cited = call(model, PROMPT.format(q=it["question"], ps="\n\n".join(f"[{k}] {p}" for k, p in enumerate(ps, 1))))
            f.write(json.dumps({"id": it["id"], "answer": ans, "passage": cited, "model": model}) + "\n")
            f.flush()
            bar.step()
    bar.finish()


def score(n):
    sample = sample50()
    per = paragraphs(sample)
    fx = fixes()
    rows = {json.loads(l)["id"]: json.loads(l) for l in open(f"{AUDIT}/sonnet50-a{n}.jsonl")}
    raw = adj = used = cited_ok = 0
    misses = []
    for it, ps in zip(sample, per):
        r = rows.get(it["id"], {})
        ans, cited = r.get("answer", ""), r.get("passage", 0)
        keys = it["aliases"]
        ok_raw = bool(ans) and nq_right(ans, keys)
        ok_adj = ok_raw or (bool(ans) and nq_right(ans, fx.get(it["id"], [])))
        raw += ok_raw
        adj += ok_adj
        used += cited > 0
        if 0 < cited <= len(ps):
            cited_ok += nq_right(ps[cited - 1], keys + fx.get(it["id"], []))
        if not ok_adj:
            holder = next((k + 1 for k, p in enumerate(ps) if nq_right(p, keys)), 0)
            misses.append(f"  {it['id']} | {it['question'][:58]}\n    key {keys[:3]} | sonnet: '{ans[:70]}' (cited {cited}) | a holder is at [{holder}]")
    print(f"attempt {n}: raw {raw} of 50; with documented key adjudications {adj} of 50; cited a paragraph {used} times, "
          f"{cited_ok} of those really held the answer")
    print(f"misses ({len(misses)}):")
    print("\n".join(misses))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "run":
        model = sys.argv[sys.argv.index("--model") + 1] if "--model" in sys.argv else "sonnet"
        run(int(sys.argv[2]), model)
    elif len(sys.argv) >= 3 and sys.argv[1] == "score":
        score(int(sys.argv[2]))
    else:
        sys.exit(__doc__)
