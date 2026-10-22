#!/usr/bin/env python3
"""Path B experiment: does a language model writing from unllm's facts beat
both the model on its own and unllm on its own?

Four setups, every question:
  model   the language model alone, from its own memory
  unllm   unllm alone
  both    unllm finds the facts; the model writes the answer from them; a
          check throws the reply away if it names a number or a proper name
          that is not in those facts, and unllm's own answer is shown instead
  wide    as "both", but the model is given unllm's 10 best search passages
          (the established retrieve-then-read way) instead of unllm's pick

Questions: a fixed random sample of Natural Questions (real search engine
questions with known short answers; never used to tune unllm) and questions
no offline system can answer, where anything but an honest refusal counts as
made up.

Decision rule, fixed before the first run: "both" goes ahead only if it gets
at least 5 points more right answers than each of the other two, makes up no
more answers than unllm alone, and takes under 5 seconds per answer.

  versus_model.py --model qwen3.5:9b [--nq 100] [--seed 7] [--out DIR]

Needs: a local ollama server, the unllm binary (build/unllm), and the Natural
Questions validation file in ~/Downloads/unllm-benchmarks/nq_open.
"""

import argparse
import json
import os
import random
import re
import statistics
import string
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
REFUSAL = re.compile(r"i couldn't find|i could not find|i don't know|i do not know|can't|cannot|can not|"
                     r"no information|not able to|unable to|don't have access|do not have access|no access|"
                     r"real-time|real time|never go online|not sure|no way to know|touches the network|"
                     r"don't hold opinions|don't write|no news", re.I)
MODEL_ALONE = ("Answer the question briefly and factually, in one or two sentences. If you cannot know the "
               "answer, say so.\n\nQuestion: {q}")
FROM_FACTS = ("Answer the question in one or two sentences, using only the facts below. If the facts do not "
              "contain the answer, reply exactly: I don't know.\n\nFacts:\n{facts}\n\nQuestion: {q}")
COMMON_CAPS = {"I", "The", "A", "An", "It", "In", "On", "At", "He", "She", "They", "This", "That", "These",
               "Those", "There", "Yes", "No", "If", "As", "Its", "His", "Her", "Their", "We", "You", "From",
               "According", "Based", "Wikipedia"}


def normalize(s):
    s = s.lower()
    s = "".join(ch for ch in s if ch not in set(string.punctuation))
    s = re.sub(r"\b(a|an|the)\b", " ", s)
    return " ".join(s.split())


def ask_unllm(binary, q):
    t = time.time()
    out = subprocess.run([binary, "--branch", "--brief", "--passages", "10", q], capture_output=True, text=True,
                         timeout=120).stdout
    ms = (time.time() - t) * 1000
    text, branch, brief, passages = [], "", [], []
    for line in out.splitlines():
        if line.startswith("[branch: "):
            branch = line[9:-1]
        elif line.startswith("[brief] "):
            brief.append(line[8:])
        elif line.startswith("[passage] "):
            passages.append(line[10:])
        elif not branch and not line.startswith("  "):
            text.append(line)
    return "\n".join(text).strip(), branch, brief, passages, ms


def ask_model(url, model, prompt):
    body = json.dumps({"model": model, "prompt": prompt, "stream": False, "think": False, "keep_alive": "15m",
                       "options": {"temperature": 0, "num_predict": 200}}).encode()
    req = urllib.request.Request(url.rstrip("/") + "/api/generate", data=body,
                                 headers={"Content-Type": "application/json"})
    t = time.time()
    with urllib.request.urlopen(req, timeout=900) as resp:
        out = json.load(resp).get("response", "")
    return re.sub(r"<think>.*?</think>", "", out, flags=re.S).strip(), (time.time() - t) * 1000


def unsupported(reply, facts, question):
    """Numbers and proper names in the reply that the facts and question never mention."""
    source = (facts + " " + question).lower().replace(",", "")
    missing = []
    for n in re.findall(r"\d[\d,.]*\d|\d", reply):
        if n.replace(",", "").rstrip(".") not in source:
            missing.append(n)
    for sent in re.split(r"(?<=[.!?])\s+", reply):
        words = re.findall(r"[A-Za-z][A-Za-z'\-]*", sent)
        for w in words[1:]:
            if w[0].isupper() and w not in COMMON_CAPS and w.lower() not in source:
                missing.append(w)
    return missing


def mark(answer, item):
    if item["kind"] == "unknowable":
        return "honest" if REFUSAL.search(answer) else "made up"
    norm = normalize(answer)
    if any(normalize(a) and normalize(a) in norm for a in item["answers"]):
        return "right"
    return "no answer" if REFUSAL.search(answer) else "wrong"


def load_items(nq_path, n, seed, repo):
    import pyarrow.parquet as pq
    rows = pq.read_table(nq_path).to_pylist()
    items = [{"id": f"nq{i}", "kind": "nq", "question": r["question"], "answers": r["answer"]}
             for i, r in enumerate(rows)]
    items = random.Random(seed).sample(items, n)
    for path, kind in ((os.path.join(repo, "tests", "general", "questions.tsv"), "limits"),
                       (os.path.join(repo, "tests", "general", "unknowable.tsv"), "unknowable")):
        with open(path, encoding="utf-8") as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if line.startswith("#") or len(parts) < 2 or parts[0] == "id":
                    continue
                if kind == "limits" and (len(parts) < 3 or parts[1] != "limits"):
                    continue
                items.append({"id": parts[0], "kind": "unknowable", "question": parts[2] if kind == "limits" else parts[1],
                              "answers": []})
    return items


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True)
    ap.add_argument("--unllm", default=os.path.join(REPO, "build", "unllm"))
    ap.add_argument("--ollama", default="http://127.0.0.1:11434")
    ap.add_argument("--nq", type=int, default=100)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--nq-file", default=os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet"))
    ap.add_argument("--out", default=os.path.expanduser("~/Downloads/unllm-corpus-sources/reports"))
    args = ap.parse_args()
    items = load_items(args.nq_file, args.nq, args.seed, REPO)
    results = []
    for i, it in enumerate(items, 1):
        r = dict(it)
        a, branch, brief, passages, ms = ask_unllm(args.unllm, it["question"])
        r["unllm"] = {"answer": a, "branch": branch, "ms": ms, "mark": mark(a, it)}
        m, mms = ask_model(args.ollama, args.model, MODEL_ALONE.format(q=it["question"]))
        r["model"] = {"answer": m, "ms": mms, "mark": mark(m, it)}
        # both: the model only writes when unllm found reference facts
        if branch.startswith("world:"):
            facts = a + ("\n" + "\n".join(brief) if brief else "")
            reply, bms = ask_model(args.ollama, args.model, FROM_FACTS.format(facts=facts, q=it["question"]))
            missing = [] if REFUSAL.search(reply) else unsupported(reply, facts, it["question"])
            final = a if missing else reply
            r["both"] = {"answer": final, "reply": reply, "rejected": missing, "ms": ms + bms, "mark": mark(final, it)}
        else:
            r["both"] = {"answer": a, "reply": "", "rejected": [], "ms": ms, "mark": mark(a, it)}
        # wide: the model reads the search's best passages, even when unllm itself gave up
        fixed_reply = branch.startswith(("calc", "category", "machine", "followup"))
        context = passages or brief
        if not fixed_reply and context:
            facts = (a + "\n" if branch.startswith("world:") else "") + "\n".join(context)
            reply, wms = ask_model(args.ollama, args.model, FROM_FACTS.format(facts=facts, q=it["question"]))
            missing = [] if REFUSAL.search(reply) else unsupported(reply, facts, it["question"])
            fallback = a if branch.startswith("world:") else "I don't know."
            final = fallback if missing else reply
            r["wide"] = {"answer": final, "reply": reply, "rejected": missing, "ms": ms + wms, "mark": mark(final, it)}
        else:
            r["wide"] = {"answer": a, "reply": "", "rejected": [], "ms": ms, "mark": mark(a, it)}
        results.append(r)
        print(f"[{i:3}/{len(items)}] {it['question'][:44]:<44} model: {r['model']['mark']:<9} "
              f"unllm: {r['unllm']['mark']:<9} both: {r['both']['mark']:<9} wide: {r['wide']['mark']}", flush=True)

    def table(setup):
        nq = [r[setup]["mark"] for r in results if r["kind"] == "nq"]
        un = [r[setup]["mark"] for r in results if r["kind"] == "unknowable"]
        ms = sorted(r[setup]["ms"] for r in results)
        right = nq.count("right")
        return (right, nq.count("no answer"), nq.count("wrong"), len(nq), un.count("honest"), un.count("made up"),
                len(un), statistics.median(ms), ms[int(0.9 * (len(ms) - 1))])

    lines = [f"path B experiment, model {args.model}, {args.nq} Natural Questions (seed {args.seed}) and "
             f"{sum(1 for r in results if r['kind'] == 'unknowable')} unknowable questions, {time.strftime('%Y-%m-%d %H:%M')}",
             "", f"{'setup':<7} {'right':>6} {'no answer':>10} {'wrong':>6}   {'honest':>7} {'made up':>8}   "
                 f"{'median':>8} {'90%':>8}"]
    stats = {}
    for setup in ("model", "unllm", "both", "wide"):
        t = table(setup)
        stats[setup] = t
        lines.append(f"{setup:<7} {t[0]:>3}/{t[3]:<3} {t[1]:>9} {t[2]:>6}   {t[4]:>3}/{t[6]:<3} {t[5]:>8}   "
                     f"{t[7] / 1000:>7.1f}s {t[8] / 1000:>7.1f}s")
    lines.append("")
    for setup in ("both", "wide"):
        rejected = sum(1 for r in results if r[setup]["rejected"])
        wrote = sum(1 for r in results if r[setup]["reply"])
        lines.append(f"'{setup}': the model wrote {wrote} answers; the check threw away {rejected} of them.")
    n = stats["model"][3] or 1
    pct = {k: 100.0 * v[0] / n for k, v in stats.items()}
    for setup in ("both", "wide"):
        go = (pct[setup] >= pct["model"] + 5 and pct[setup] >= pct["unllm"] + 5 and
              stats[setup][5] <= stats["unllm"][5] and stats[setup][7] < 5000)
        lines.append(f"Decision rule for '{setup}' (5 points over model and unllm, no more made up than unllm, "
                     f"under 5 s): {'MET' if go else 'NOT MET'} (right: model {pct['model']:.0f}%, "
                     f"unllm {pct['unllm']:.0f}%, {setup} {pct[setup]:.0f}%)")
    text = "\n".join(lines)
    print("\n" + text)
    stamp = time.strftime("%Y-%m-%d-%H%M") + "-pathb-" + re.sub(r"[^a-z0-9.]+", "-", args.model.lower())
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, stamp + ".txt"), "w") as f:
        f.write(text + "\n")
    with open(os.path.join(args.out, stamp + ".json"), "w") as f:
        json.dump(results, f, indent=1, ensure_ascii=False)
    print(f"saved: {args.out}/{stamp}.txt and .json")


if __name__ == "__main__":
    sys.exit(main())
