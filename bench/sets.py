#!/usr/bin/env python3
"""Round 1 test items: one JSONL file per job in DATA/sets/items-<job>.jsonl.

Each item: {"job", "id", "input" (the text the model reads), "choices"
(a list, or null for a written answer), "key" (a trusted answer when one
exists, else null), "meta"}. The same instruction (jobs.py) goes to Haiku,
which writes the reference answers, and to every small model.

  sets.py            build every job's items (fixed seeds, so reruns match)

Inputs: QReCC test conversations, chat39 (with ask's replies from the
2026-10-17 pilot), the clean dev set, 300 NQ questions with the passages
unllm found for them, TriviaQA validation, and machine questions mined from
public chat logs that ask refuses.
"""

import csv
import json
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
from jobs import DATA, JOBS  # noqa: E402

HOME = os.path.expanduser("~")
BENCH = HOME + "/Downloads/unllm-benchmarks"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PILOT = HOME + "/.local/share/activityscope/versus/2026-10-17-pilot/results.jsonl"
CHAT39 = HOME + "/.local/share/activityscope/versus/chat39/script.txt"
REFUSALS = ("I can't answer", "doesn't seem to be a question", "I half understood")


def convo_text(turns, latest):
    """turns: [(user, assistant)], shown oldest first, replies cut to 300 characters."""
    lines = []
    for u, a in turns:
        lines.append(f"User: {u}")
        a = " ".join(a.split())
        lines.append(f"Assistant: {a[:300]}{'...' if len(a) > 300 else ''}")
    return ("Conversation so far:\n" + ("\n".join(lines) if lines else "(none)") +
            f"\n\nLatest user message: {latest}")


def qrecc_turns(n_convs=30, seed=7):
    data = json.load(open(BENCH + "/qrecc/data/qrecc_test.json"))
    by = {}
    for r in data:
        by.setdefault((r["Conversation_source"], r["Conversation_no"]), []).append(r)
    keys = sorted(k for k, v in by.items() if len(v) >= 4)
    out = []
    for k in random.Random(seed).sample(keys, n_convs):
        for r in sorted(by[k], key=lambda r: r["Turn_no"]):
            ctx = r["Context"]
            pairs = [(ctx[i], ctx[i + 1] if i + 1 < len(ctx) else "") for i in range(0, len(ctx), 2)]
            out.append({"id": f"q{k[1]}-{r['Turn_no']}", "convo": convo_text(pairs, r["Question"]),
                        "question": r["Question"], "rewrite": r["Rewrite"], "turn": r["Turn_no"]})
    return out


def chat39_turns():
    turns = [l.rstrip("\n") for l in open(CHAT39) if l.strip() and not l.startswith("#")]
    replies = {}
    for line in open(PILOT):
        r = json.loads(line)
        if r.get("set") == "chat" and r.get("setup") == "ask":
            replies[r["n"]] = r["reply"]
    out, hist = [], []
    for n, q in enumerate(turns, 1):
        out.append({"id": f"c39-{n}", "convo": convo_text(hist[-6:], q), "question": q, "turn": n})
        hist.append((q, replies.get(n, "")))
    return out


def dev_rows():
    with open(REPO + "/tests/clean/dev.tsv") as f:
        return list(csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t"))


def cached(name):
    out = {}
    for line in open(f"{DATA}/sets/pcache-{name}.jsonl"):
        c = json.loads(line)
        out[c["question"]] = c["reply"]["passages"]
    return out


def titles_of(passages, n=10):
    seen = []
    for p in passages:
        t = p.split(": ", 1)[0]
        if t not in seen:
            seen.append(t)
        if len(seen) == n:
            break
    return seen


def nq_rows(n=300, seed=11):
    from evaluate import nq_items
    return nq_items(n, seed, None)


def trivia_rows(n=100, seed=3):
    import pyarrow.parquet as pq
    rows = pq.read_table(BENCH + "/trivia_qa/validation-rc-nocontext.parquet", columns=["question", "question_id"]).to_pylist()
    return random.Random(seed).sample(rows, n)


def refused_machine_lines(n=50, seed=5):
    lines = []
    for f in ("wildchat-machine.txt", "lmsys-machine.txt"):
        for l in open(HOME + "/Downloads/unllm-corpus-sources/mined/" + f):
            parts = l.rstrip("\n").split("\t", 1)
            if len(parts) == 2 and 3 <= len(parts[1].split()) <= 20:
                lines.append(parts[1])
    rnd = random.Random(seed)
    rnd.shuffle(lines)
    out = []
    for q in lines:
        a = subprocess.run([HOME + "/.local/bin/ask", q], capture_output=True, text=True, timeout=60).stdout
        if any(r in a for r in REFUSALS):
            out.append(q)
        if len(out) == n:
            break
    return out


def build():
    os.makedirs(DATA + "/sets", exist_ok=True)
    items = {j: [] for j in JOBS}
    convs = qrecc_turns() + chat39_turns()
    for c in convs:
        items["turnkind"].append({"id": c["id"], "input": c["convo"], "key": None, "meta": {"question": c["question"]}})
        if c["turn"] > 1:
            items["rewrite"].append({"id": c["id"], "input": c["convo"], "key": c.get("rewrite"),
                                     "meta": {"question": c["question"]}})
    dev = dev_rows()
    pdev, pnq = cached("dev"), cached("nq")
    nq = nq_rows()
    for r in dev:
        ts = titles_of(pdev.get(r["question"], []))
        if ts:
            items["article"].append({"id": r["id"], "input": f"Question: {r['question']}\n\nArticles found:\n" +
                                     "\n".join(f"- {t}" for t in ts), "choices": ts + ["none of these"],
                                     "key": r["source_title"] if r["source_title"] in ts else
                                     ("none of these" if r["source_title"] else None),
                                     "meta": {"question": r["question"], "unllm_first": ts[0]}})
    for r in nq[:100]:
        ts = titles_of(pnq.get(r["question"], []))
        if ts:
            items["article"].append({"id": r["id"], "input": f"Question: {r['question']}\n\nArticles found:\n" +
                                     "\n".join(f"- {t}" for t in ts), "choices": ts + ["none of these"], "key": None,
                                     "meta": {"question": r["question"], "unllm_first": ts[0]}})
    for r in dev:
        items["answertype"].append({"id": r["id"], "input": f"Question: {r['question']}", "key": None,
                                    "meta": {"dev_type": r["answer_type"]}})
    for r in nq[:100]:
        items["answertype"].append({"id": r["id"], "input": f"Question: {r['question']}", "key": None, "meta": {}})
    for r in trivia_rows():
        items["answertype"].append({"id": "tq-" + r["question_id"], "input": f"Question: {r['question']}", "key": None,
                                    "meta": {}})
    for i, q in enumerate(refused_machine_lines(), 1):
        items["reword"].append({"id": f"rf{i:02d}", "input": f"Message: {q}", "key": None, "meta": {"question": q}})
    sys.path.insert(0, REPO + "/tools")
    from qa_score import nq_right, pattern_right
    sample = dev[:50]
    for r in sample + nq[:50]:
        ps = (pdev if r in sample else pnq).get(r["question"], [])[:10]
        if not ps:
            continue
        body = "\n\n".join(f"[{i}] {' '.join(p.split())[:400]}" for i, p in enumerate(ps, 1))
        hit = next((str(i) for i, p in enumerate(ps, 1)
                    if (pattern_right(p, r["answer_pattern"]) if "answer_pattern" in r else nq_right(p, r["aliases"]))),
                   "none")
        items["passage"].append({"id": r["id"], "input": f"Question: {r['question']}\n\nPassages:\n{body}",
                                 "choices": [str(i) for i in range(1, len(ps) + 1)] + ["none"], "key": None,
                                 "meta": {"question": r["question"], "answer_string_in": hit}})
    # The checker: a right and a wrong claim per question, against the
    # passage the right answer came from (known by construction).
    rows = [json.loads(l) for l in open(DATA + "/sets/cmp-dev-base.jsonl")]
    q2p = pdev
    for r in rows:
        right = [s for s in r["spans"] if s["right"]]
        wrong = [s for s in r["spans"] if not s["right"] and s["text"].strip()]
        ps = q2p.get(r["question"], [])
        if not right or not wrong or not ps:
            continue
        g = max(right, key=lambda s: s["diff"])
        b = max(wrong, key=lambda s: s["diff"])
        if g["rank"] >= len(ps) or g["text"].lower() in b["text"].lower() or b["text"].lower() in g["text"].lower():
            continue
        passage = " ".join(ps[g["rank"]].split())[:700]
        for tag, s, k in (("y", g, "yes"), ("n", b, "no")):
            items["checker"].append({"id": f"{r['id']}{tag}", "input": f"Passage: {passage}\n\nQuestion: {r['question']}\n"
                                     f"Claimed answer: {s['text']}", "choices": ["yes", "no"], "key": k,
                                     "meta": {"question": r["question"]}})
        if len(items["checker"]) >= 200:
            break
    for job, its in items.items():
        with open(f"{DATA}/sets/items-{job}.jsonl", "w") as f:
            for it in its:
                it.setdefault("choices", JOBS[job].get("choices"))
                it["job"] = job
                f.write(json.dumps(it) + "\n")
        print(f"{job:11} {len(its):4} items")


if __name__ == "__main__":
    build()
