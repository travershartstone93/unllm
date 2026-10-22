#!/usr/bin/env python3
"""How often unllm's search finds the answer at all.

For each Natural Questions question, unllm is asked with --passages 20, and
the accepted answers are looked for in its first 1, 5, 10 and 20 passages
(one passage per document, best first). When unllm answered straight from a
fact box or an opening line, that answer counts as its first passage.

This is the ceiling for any helper built on unllm: an answer the search never
surfaces cannot be read, judged or written up by anything downstream.

  recall.py [--n 300] [--seed 11] [--out DIR]

Results are saved after every question, and the run pauses while the CPU is
above 85 C.
"""

import argparse
import glob
import json
import os
import random
import re
import string
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
CUTS = (1, 5, 10, 20)


def normalize(s):
    s = s.lower()
    s = "".join(ch for ch in s if ch not in set(string.punctuation))
    s = re.sub(r"\b(a|an|the)\b", " ", s)
    return " ".join(s.split())


def cpu_temp():
    best = None
    for hw in glob.glob("/sys/class/hwmon/hwmon*"):
        try:
            if open(os.path.join(hw, "name")).read().strip() not in ("coretemp", "k10temp", "zenpower"):
                continue
            for t in glob.glob(os.path.join(hw, "temp*_input")):
                v = int(open(t).read()) / 1000.0
                best = v if best is None else max(best, v)
        except (OSError, ValueError):
            pass
    return best


def cool_down(limit=85.0):
    t = cpu_temp()
    while t is not None and t > limit:
        print(f"  cpu at {t:.0f} C, pausing 30 s", flush=True)
        time.sleep(30)
        t = cpu_temp()


def search(binary, q):
    out = subprocess.run([binary, "--branch", "--brief", "--passages", "20", q], capture_output=True, text=True,
                         timeout=180).stdout
    answer, branch, brief, passages = [], "", [], []
    for line in out.splitlines():
        if line.startswith("[branch: "):
            branch = line[9:-1]
        elif line.startswith("[brief] "):
            brief.append(line[8:])
        elif line.startswith("[passage] "):
            passages.append(line[10:])
        elif not branch and not line.startswith("  "):
            answer.append(line)
    answer = " ".join(answer).strip()
    # A direct answer (fact box, opening line) has no ranked list: it is the list.
    if not passages and branch.startswith("world:"):
        passages = [answer] + brief
    return branch, answer, passages


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=300)
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--unllm", default=os.path.join(REPO, "build", "unllm"))
    ap.add_argument("--nq-file", default=os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet"))
    ap.add_argument("--out", default=os.path.expanduser("~/Downloads/unllm-corpus-sources/reports"))
    args = ap.parse_args()
    import pyarrow.parquet as pq
    rows = pq.read_table(args.nq_file).to_pylist()
    sample = random.Random(args.seed).sample(rows, args.n)
    os.makedirs(args.out, exist_ok=True)
    stamp = time.strftime("%Y-%m-%d-%H%M") + f"-recall-n{args.n}"
    jpath = os.path.join(args.out, stamp + ".jsonl")
    hits = {k: 0 for k in CUTS}
    answer_right = 0
    with open(jpath, "w") as jf:
        for i, r in enumerate(sample, 1):
            cool_down()
            branch, answer, passages = search(args.unllm, r["question"])
            aliases = [normalize(a) for a in r["answer"] if normalize(a)]
            first = None
            for rank, p in enumerate(passages[:20], 1):
                text = normalize(p)
                if any(a in text for a in aliases):
                    first = rank
                    break
            for k in CUTS:
                hits[k] += 1 if first is not None and first <= k else 0
            right = any(a in normalize(answer) for a in aliases)
            answer_right += right
            jf.write(json.dumps({"question": r["question"], "answers": r["answer"], "branch": branch,
                                 "first_hit": first, "answer_right": right, "passages": len(passages)}) + "\n")
            jf.flush()
            if i % 25 == 0:
                print(f"[{i}/{args.n}] " + "  ".join(f"top {k}: {100 * hits[k] / i:.0f}%" for k in CUTS) +
                      f"  unllm's answer: {100 * answer_right / i:.0f}%", flush=True)
    n = args.n
    text = "\n".join([f"search recall on {n} Natural Questions (seed {args.seed}), {time.strftime('%Y-%m-%d %H:%M')}",
                      "answer found in unllm's first passages:"] +
                     [f"  top {k:<2}  {hits[k]:>4} of {n}  ({100 * hits[k] / n:.0f}%)" for k in CUTS] +
                     [f"unllm's own answer right: {answer_right} of {n} ({100 * answer_right / n:.0f}%)"])
    print("\n" + text)
    with open(os.path.join(args.out, stamp + ".txt"), "w") as f:
        f.write(text + "\n")
    print(f"saved: {args.out}/{stamp}.txt and .jsonl")


if __name__ == "__main__":
    sys.exit(main())
