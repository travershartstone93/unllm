#!/usr/bin/env python3
"""qwen3:4b alone (no unllm, no passages) on the verified dev 50. Answers saved."""
import json, os, re, sys, urllib.request
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402
A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
P = 'Answer from your own knowledge with the shortest exact answer (a name, date, place or number). Never say you don\'t know.\n\nQuestion: {q}\n\nReply with only JSON: {{"answer": "..."}}'


def ask(q):
    body = json.dumps({"model": "qwen3:4b", "messages": [{"role": "user", "content": P.format(q=q)}], "stream": False,
                       "format": "json", "think": False, "options": {"temperature": 0}}).encode()
    req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            return str(json.loads(json.loads(r.read())["message"]["content"]).get("answer", "")).strip()
    except (ValueError, OSError, KeyError, TypeError):
        return ""


rows = [l.split("\t") for l in open(f"{A}/dev50.tsv").read().splitlines()[1:]]
bar = Task("fourb50", "4B alone, 50 questions", total=len(rows))
out = []
for i, q, pat, t, src in rows:
    a = ask(q)
    out.append({"id": i, "question": q, "answer": a, "right": bool(a) and re.search(pat, a, re.I) is not None})
    bar.step(detail=i)
bar.finish()
open(f"{A}/fourb-alone50.jsonl", "w").write("".join(json.dumps(r) + "\n" for r in out))
line = f"- qwen3:4b alone, from memory, verified dev 50: {sum(r['right'] for r in out)} of 50"
print(line)
for r in out:
    if not r["right"]:
        print(f"  miss {r['id']} {r['question'][:50]} | 4b: {r['answer'][:60]}")
open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a").write(line + "\n")
