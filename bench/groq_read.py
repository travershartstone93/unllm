#!/usr/bin/env python3
"""Can a strong hosted model turn found passages into right answers? Groq's
qwen3.8-27b answers the 400 vector-pilot questions twice: from memory alone,
and with the 20 vector-search passages (one per article, cut to 450
characters), told to use its own knowledge when the passages do not help.

  groq_read.py [memory|passages]      default: both

Scored right out of 400 against the answer keys (whole-word match); a refusal
or "I don't know" is a wrong answer. Rate limits: honours Retry-After on 429
and paces requests. Resumable: DATA/vec/groq-<mode>.jsonl. Tray bar.
"""

import json
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import readers  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from taskprogress import Task  # noqa: E402

VEC = f"{DATA}/vec"
MODEL = "qwen/qwen3.8-27b"
URL = "https://api.groq.com/openai/v1/chat/completions"
PACE = 2.5
SYSTEM_MEMORY = ("Answer the question from your own knowledge with the shortest correct answer (a name, date, place "
                 "or number). Always give your best answer; never say you don't know. Reply with only JSON: "
                 "{\"answer\": \"...\"}")
SYSTEM_PASSAGES = ("You are given a question and passages from an encyclopedia. If a passage holds the answer, take "
                   "it from there; otherwise answer from your own knowledge. Give the shortest correct answer (a "
                   "name, date, place or number). Always give your best answer; never say you don't know. Reply "
                   "with only JSON: {\"answer\": \"...\"}")


def api_key():
    for line in open(os.path.expanduser(os.environ["APIS_ENV"])):
        if line.startswith("GROQ_API_KEY="):
            return line.strip().split("=", 1)[1]
    raise SystemExit("no GROQ_API_KEY in APIS_ENV")


def parse(text):
    for cand in reversed(re.findall(r"\{[^{}]*\"answer\"[^{}]*\}", text or "", re.S)):
        try:
            return str(json.loads(cand).get("answer", "")).strip()
        except ValueError:
            continue
    return None


def call(key, system, user, parse_fn=parse):
    body = {"model": MODEL, "temperature": 0, "max_tokens": 300, "reasoning_effort": "none",
            "messages": [{"role": "system", "content": system}, {"role": "user", "content": user}]}
    code = "000"
    for attempt in range(8):
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(body, f)
            path = f.name
        hdr = path + ".hdr"
        try:
            r = subprocess.run(["curl", "-4", "-s", "-m", "120", "-D", hdr, "-w", "\n%{http_code}", URL,
                                "-H", "Content-Type: application/json", "-H", "Authorization: Bearer " + key,
                                "-d", "@" + path], capture_output=True, text=True, timeout=130)
            head = open(hdr).read() if os.path.exists(hdr) else ""
        finally:
            os.unlink(path)
            if os.path.exists(hdr):
                os.unlink(hdr)
        out, _, code = r.stdout.rpartition("\n")
        if code == "200":
            try:
                return parse_fn(json.loads(out)["choices"][0]["message"]["content"]), None
            except (KeyError, IndexError, ValueError, TypeError):
                return None, "bad reply: " + out[:120]
        if code == "400" and "reasoning_effort" in out and "reasoning_effort" in body:
            del body["reasoning_effort"]  # the model does not take the switch: go without it
            continue
        wait = 20
        m = re.search(r"retry-after:\s*(\d+)", head, re.I)
        if m:
            wait = int(m.group(1)) + 1
        print(f"  HTTP {code}, waiting {wait}s: {out[:100]}", flush=True)
        time.sleep(wait if code == "429" else min(wait * (attempt + 1), 120))
    return None, f"gave up: HTTP {code}"


def run(mode, key, items, passages, bar):
    path = f"{VEC}/groq-{mode}.jsonl"
    done = {json.loads(l)["id"] for l in open(path)} if os.path.exists(path) else set()
    bar.step(len(done))
    with open(path, "a") as f:
        for it in items:
            if it["id"] in done:
                continue
            if mode == "memory":
                ans, err = call(key, SYSTEM_MEMORY, "Question: " + it["question"])
            else:
                ps = readers.one_per_article(passages[it["id"]])[:20]
                body = "\n\n".join(f"[{k}] {' '.join(p.split())[:450]}" for k, p in enumerate(ps, 1))
                ans, err = call(key, SYSTEM_PASSAGES, f"Question: {it['question']}\n\nPassages:\n{body}")
            f.write(json.dumps({"id": it["id"], "answer": ans, "error": err,
                                "right": bool(ans and nq_right(ans, it["aliases"]))}) + "\n")
            f.flush()
            bar.step(detail=mode)
            time.sleep(PACE)


def main():
    modes = sys.argv[1:] or ["memory", "passages"]
    key = api_key()
    items = [json.loads(l) for l in open(f"{VEC}/testset.jsonl")]
    passages = {json.loads(l)["id"]: json.loads(l)["passages"] for l in open(f"{VEC}/vector.jsonl")}
    bar = Task("groq-read", "Groq qwen3.8-27b: memory vs passages", total=len(modes) * len(items))
    for mode in modes:
        run(mode, key, items, passages, bar)
    bar.finish()
    lines = ["\n### Groq qwen3.8-27b reading test"]
    for mode in modes:
        rows = [json.loads(l) for l in open(f"{VEC}/groq-{mode}.jsonl")]
        right = sum(r["right"] for r in rows)
        errs = sum(1 for r in rows if r["error"])
        lines.append(f"- {mode}: right {right} of {len(rows)} ({100 * right / len(rows):.0f}%), {errs} failed requests")
    text = "\n".join(lines)
    print(text)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md"), "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    main()
