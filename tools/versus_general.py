#!/usr/bin/env python3
"""Score unllm, and optionally a local language model, on the general-knowledge
exam (tests/general/questions.tsv).

Each answer is marked:
  right     it contains the expected fact (a case-insensitive pattern)
  no answer it honestly says it does not know (unllm only; never counted wrong)
  wrong     anything else, including a confident answer with the wrong fact

  versus_general.py [--unllm PATH] [--model NAME] [--ollama URL] [--only unllm|model|both]
                    [--limit N] [--out DIR]

The model is asked through a local ollama server, one question at a time, at
temperature 0. Reports go to ~/Downloads/unllm-corpus-sources/reports by
default: a summary text file and a JSON file with every answer.
"""

import argparse
import collections
import json
import os
import re
import statistics
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
NO_ANSWER = re.compile(r"^(I couldn't find that|No reference packs are installed|That is a question about this machine)")
PROMPT = ("Answer the question briefly and factually, in one to three sentences. "
          "If you cannot know the answer, say so.\n\nQuestion: {q}")


def load_questions(path):
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 4 or parts[0] == "id":
                continue
            qid, cat, q, expect = parts
            out.append({"id": qid, "category": cat, "question": q, "expect": expect})
    return out


def ask_unllm(binary, q):
    t = time.time()
    r = subprocess.run([binary, "--branch", q], capture_output=True, text=True, timeout=120)
    ms = (time.time() - t) * 1000
    text, branch = [], ""
    for line in r.stdout.splitlines():
        if line.startswith("[branch: "):
            branch = line[9:-1]
            break
        text.append(line)
    return "\n".join(text).strip(), branch, ms


def ask_model(url, model, q):
    body = json.dumps({"model": model, "prompt": PROMPT.format(q=q), "stream": False, "think": False,
                       "options": {"temperature": 0, "num_predict": 300}}).encode()
    req = urllib.request.Request(url.rstrip("/") + "/api/generate", data=body,
                                 headers={"Content-Type": "application/json"})
    t = time.time()
    with urllib.request.urlopen(req, timeout=600) as resp:
        out = json.load(resp).get("response", "")
    ms = (time.time() - t) * 1000
    return re.sub(r"<think>.*?</think>", "", out, flags=re.S).strip(), ms


def mark(item, answer, honest_none):
    if re.search(item["expect"], answer, re.I):
        return "right"
    if honest_none and item["category"] != "limits":
        return "no answer"
    return "wrong"


def summary(results, who):
    rows = collections.OrderedDict()
    for r in results:
        if who not in r:
            continue
        c = rows.setdefault(r["category"], collections.Counter())
        c[r[who]["mark"]] += 1
        c["n"] += 1
    lines = []
    total = collections.Counter()
    for cat, c in rows.items():
        total.update(c)
        lines.append(f"  {cat:<10} {c['right']:>3} right  {c['no answer']:>3} no answer  {c['wrong']:>3} wrong   of {c['n']}")
    if total["n"]:
        lines.append(f"  {'ALL':<10} {total['right']:>3} right  {total['no answer']:>3} no answer  {total['wrong']:>3} wrong   "
                     f"of {total['n']}  ({100 * total['right'] / total['n']:.0f}% right)")
        times = [r[who]["ms"] for r in results if who in r]
        lines.append(f"  time per question: median {statistics.median(times):.0f} ms, slowest {max(times):.0f} ms")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--unllm", default=os.path.join(REPO, "build", "unllm"))
    ap.add_argument("--model")
    ap.add_argument("--ollama", default="http://127.0.0.1:11434")
    ap.add_argument("--only", choices=["unllm", "model", "both"], default="both")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--questions", default=os.path.join(REPO, "tests", "general", "questions.tsv"))
    ap.add_argument("--out", default=os.path.expanduser("~/Downloads/unllm-corpus-sources/reports"))
    ap.add_argument("--skip", default="", help="question ids to leave out, comma separated")
    ap.add_argument("--quiet", action="store_true",
                    help="print only the totals (for the held-out set: its answers are never looked at)")
    args = ap.parse_args()
    skip = {x.strip() for x in args.skip.split(",") if x.strip()}
    items = [it for it in load_questions(args.questions) if it["id"] not in skip]
    if args.limit:
        items = items[:args.limit]
    run_unllm = args.only in ("unllm", "both")
    run_model = args.only in ("model", "both") and args.model
    results = []
    for i, it in enumerate(items, 1):
        r = dict(it)
        if run_unllm:
            a, branch, ms = ask_unllm(args.unllm, it["question"])
            r["unllm"] = {"answer": a, "branch": branch, "ms": ms,
                          "mark": mark(it, a, branch in ("none", "machine") or bool(NO_ANSWER.match(a)))}
        if run_model:
            try:
                a, ms = ask_model(args.ollama, args.model, it["question"])
            except Exception as e:  # a failed request is reported, not fatal
                a, ms = f"(request failed: {e})", 0.0
            r["model"] = {"answer": a, "ms": ms, "mark": mark(it, a, False)}
        results.append(r)
        if not args.quiet:
            marks = "  ".join(f"{k}: {r[k]['mark']}" for k in ("unllm", "model") if k in r)
            print(f"[{i:3}/{len(items)}] {it['id']} {it['question'][:50]:<50} {marks}", flush=True)
    stamp = time.strftime("%Y-%m-%d-%H%M") + "-" + os.path.splitext(os.path.basename(args.questions))[0]
    os.makedirs(args.out, exist_ok=True)
    report = [f"{os.path.basename(args.questions)}: {len(items)} questions, {time.strftime('%Y-%m-%d %H:%M')}"]
    if run_unllm:
        report += ["", "unllm", summary(results, "unllm")]
    if run_model:
        report += ["", f"local model: {args.model}", summary(results, "model")]
    text = "\n".join(report)
    print("\n" + text)
    with open(os.path.join(args.out, f"{stamp}.txt"), "w") as f:
        f.write(text + "\n")
    with open(os.path.join(args.out, f"{stamp}.json"), "w") as f:
        json.dump(results, f, indent=1, ensure_ascii=False)
    print(f"\nsaved: {args.out}/{stamp}.txt and .json")


if __name__ == "__main__":
    sys.exit(main())
