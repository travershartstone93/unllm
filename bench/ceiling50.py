#!/usr/bin/env python3
"""Ceiling test (2026-10-19): for the same 50 questions and the same 20 paragraphs as
reader50.py, does any paragraph STATE the answer in a sentence? The local qwen3:4b (Ollama)
is the judge: it names the paragraph and quotes the sentence; a question counts as answerable
only if the quoted sentence occurs in that paragraph and contains the answer key.
Decision rule, written first: under 30 answerable -> the material is the ceiling (corpus and
test set next); 35 or more -> the paragraph picker is the gap (DPR reader next).
"""

import json
import os
import random
import re
import subprocess
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vec_pilot  # noqa: E402
from qa_score import nq_right  # noqa: E402
from spine_audit import AUDIT, REPORT  # noqa: E402

TOOL = os.path.expanduser("~/repo-work/unllm/build/unllm_meaning")
INDEX = os.path.expanduser("~/.local/share/unllm/vectors/wikipedia-simple.unlpq")
MODEL = os.path.expanduser("~/.local/share/unllm/models/bge-small-en-v1.5")
OLLAMA = "http://localhost:11434"
JUDGE = "qwen3:4b"
PROMPT = ("You are checking reference paragraphs. Question: {q}\n\nParagraphs:\n{ps}\n\n"
          "Does any paragraph contain a sentence that directly states the answer to the question? "
          "If yes, give the paragraph number and copy that one sentence exactly as written. If no paragraph states it, say so. "
          "Reply with only JSON: {{\"stated\": true or false, \"paragraph\": N or 0, \"sentence\": \"...\"}}")


def ask(prompt):
    body = json.dumps({"model": JUDGE, "messages": [{"role": "user", "content": prompt}], "stream": False, "format": "json",
                       "think": False, "options": {"temperature": 0, "num_ctx": 12288}}).encode()
    req = urllib.request.Request(f"{OLLAMA}/api/chat", data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return json.loads(r.read())["message"]["content"]


def norm(s):
    return re.sub(r"\s+", " ", s.lower()).strip()


def main():
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    random.seed(7)
    sample = random.sample(items, 50)
    out = subprocess.run([TOOL, INDEX, MODEL, "20", "64"], input="".join(it["question"] + "\n" for it in sample),
                         capture_output=True, text=True).stdout
    ids = [json.loads(l)["ids"] for l in out.splitlines() if l.startswith("{")]
    con = vec_pilot.shard()
    arms = {r["id"]: r for r in vec_pilot.load(f"{AUDIT}/reader50.jsonl")}
    stated = said_yes = unverified = 0
    where = {"first": 0, "2-5": 0, "6-20": 0}
    rules_on_stated = reader_on_stated = 0
    rows = []
    for it, idl in zip(sample, ids):
        ps = []
        for i in idl:
            row = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid = ?",
                              (i & 0xffffffff,)).fetchone()
            if row:
                ps.append(f"{row[0]}: {' '.join(row[1].split())[:900]}")
        text = "\n\n".join(f"[{k}] {p}" for k, p in enumerate(ps, 1))
        try:
            got = json.loads(ask(PROMPT.format(q=it["question"], ps=text)))
        except (ValueError, OSError):
            got = {}
        yes = bool(got.get("stated"))
        n = int(got.get("paragraph") or 0)
        sent = str(got.get("sentence") or "")
        ok = yes and 0 < n <= len(ps) and len(norm(sent)) > 10 and norm(sent)[:60] in norm(ps[n - 1]) and nq_right(sent, it["aliases"])
        said_yes += yes
        unverified += yes and not ok
        stated += ok
        if ok:
            where["first" if n == 1 else "2-5" if n <= 5 else "6-20"] += 1
            rules_on_stated += arms[it["id"]]["rules_right"]
            reader_on_stated += arms[it["id"]]["reader_right"]
        rows.append({"id": it["id"], "question": it["question"], "key": it["aliases"][0], "judge_yes": yes, "paragraph": n,
                     "sentence": sent[:200], "verified": ok})
    with open(f"{AUDIT}/ceiling50.jsonl", "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    urllib.request.urlopen(urllib.request.Request(f"{OLLAMA}/api/generate", data=json.dumps({"model": JUDGE, "keep_alive": 0}).encode(),
                                                  headers={"Content-Type": "application/json"}), timeout=60).read()
    lines = [f"judge said a paragraph states the answer for {said_yes} of 50; verified (sentence found in that paragraph and holding the key): {stated}; "
             f"unverifiable claims: {unverified}",
             f"where the stating paragraph sits: first {where['first']}, 2-5 {where['2-5']}, 6-20 {where['6-20']}",
             f"of the {stated} answerable: rules right {rules_on_stated}, reader right {reader_on_stated}",
             f"decision: {'material is the ceiling (under 30)' if stated < 30 else 'picker is the gap (35 or more)' if stated >= 35 else 'between the two thresholds'}"]
    print("\n".join(lines))
    with open(REPORT, "a") as f:
        f.write("\n### Ceiling test, local qwen3:4b as judge, same 50 (2026-10-19)\n" + "".join(f"- {l}\n" for l in lines))


if __name__ == "__main__":
    main()
