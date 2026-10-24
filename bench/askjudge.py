#!/usr/bin/env python3
"""Does ask's answer answer the question? Every question of a set through ask; Opus 5.5 judges each answer:
answers / partly / no / honest. Rows -> DATA/audit/askjudge-LABEL-SET.jsonl, totals to the report.
  askjudge.py LABEL SET.json [PREV_LABEL]
With PREV_LABEL, an answer that matches that run's answer to the same question (digits aside) keeps its
verdict, and only the changed answers are judged: one judging run moves by about 5 in 135 on its own.
"""
import json, os, re, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
GOLD = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/gold")
ASK = os.environ.get("ASK_CLI") or os.path.expanduser("~/.local/bin/ask")
JUDGE = """An assistant answers questions about the user's own Linux laptop from that laptop's recorded history and live readings. For each item below, judge ONLY whether the answer responds to the question that was asked. You cannot check the figures; assume they are true. Verdicts:
- "answers": it responds to what was asked, on the right subject and the right time (now versus the past time asked about).
- "partly": on the right subject but misses the specific thing asked (a different figure, the wrong time, a general summary where something specific was asked).
- "no": it answers a different question, or asks for clarification on a clear question.
- "honest": it says it cannot answer that, and the question really is outside what such records could hold.
Give a few words of reason.

Items:
{items}

Reply with only a JSON list: [{{"item": N, "verdict": "answers|partly|no|honest", "why": "..."}}, ...]"""


def same(a, b):
    return re.sub(r"\d+", "#", a) == re.sub(r"\d+", "#", b)


def main(label, name, prev=None):
    rows = json.load(open(f"{A}/{name}"))
    before = {}
    if prev:
        for line in open(f"{A}/askjudge-{prev}-{name.replace('.json', '')}.jsonl"):
            x = json.loads(line)
            before[x["question"]] = x
    bar = Task("askjudge", f"ask answers judged: {label}", total=len(rows) + 2)
    for r in rows:
        out = subprocess.run([ASK, "--branch", r["question"]], capture_output=True, text=True, errors="replace", timeout=300).stdout
        r["answer"] = " ".join(l for l in out.split("\n\n")[0].splitlines() if not l.startswith("[")).strip()
        r["branch"] = next((l[9:-1] for l in out.splitlines() if l.startswith("[branch: ")), "")
        bar.step(detail=r["question"][:30])
    verdicts = {}
    todo = []
    for i, r in enumerate(rows, 1):
        b = before.get(r["question"])
        if b and b.get("verdict") not in (None, "unjudged") and same(b["answer"], r["answer"]):
            verdicts[i] = {"verdict": b["verdict"], "why": b.get("why", ""), "carried": True}
        else:
            todo.append(i)
    print(f"{len(todo)} changed answers to judge, {len(rows) - len(todo)} carried")
    for half in (0, 1):
        part = todo[half * (len(todo) + 1) // 2:(half + 1) * (len(todo) + 1) // 2]
        if not part:
            bar.step(detail="judge")
            continue
        items = "\n\n".join(f"[{i}] question: {rows[i - 1]['question']}\nanswer: {rows[i - 1]['answer'][:700]}" for i in part)
        raw = subprocess.run(["claude", "-p", "--model", "claude-opus-5-5", JUDGE.format(items=items)], capture_output=True, text=True,
                             timeout=1200, stdin=subprocess.DEVNULL, cwd=GOLD).stdout
        m = re.search(r"\[.*\]", raw, re.S)
        for v in (json.loads(m.group(0)) if m else []):
            if isinstance(v, dict): verdicts[int(v["item"])] = v
        bar.step(detail="judge")
    bar.finish()
    tot = {}
    with open(f"{A}/askjudge-{label}-{name.replace('.json', '')}.jsonl", "w") as f:
        for i, r in enumerate(rows, 1):
            v = verdicts.get(i, {"verdict": "unjudged", "why": ""})
            r["verdict"], r["why"], r["carried"] = v.get("verdict", "unjudged"), v.get("why", ""), v.get("carried", False)
            tot[r["verdict"]] = tot.get(r["verdict"], 0) + 1
            f.write(json.dumps(r) + "\n")
    line = f"- {name}, {label}: " + ", ".join(f"{k} {v}" for k, v in sorted(tot.items())) + f" of {len(rows)}" + \
        (f" ({len(todo)} changed answers judged, the rest carried from {prev})" if prev else "")
    print(line)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write(line + "\n")


if __name__ == "__main__":
    main(*sys.argv[1:4])
