#!/usr/bin/env python3
"""Score Round 1: every model's answers against Haiku's, per job, with the
simple baselines beside them (unllm's own pick, always-the-commonest).

  score.py [--out FILE]

Menu jobs: right = same choice as Haiku. Written jobs (rewrite, reword):
Haiku judges whether the model's question asks the same thing as the
reference (QReCC's human rewrite where there is one); for reword, "none"
must match "none". A missing or badly formed reply counts as wrong.
Also reports how often Haiku agrees with the trusted keys that exist
(dev source articles, the checker's built-in answers), so the foundation's
own error rate is known. Judgments are cached in DATA/judge.jsonl.
"""

import collections
import hashlib
import json
import os
import re
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gold import haiku, parse_list  # noqa: E402
from jobs import DATA, JOBS  # noqa: E402

JUDGE = DATA + "/judge.jsonl"


def load(path):
    return {json.loads(l)["id"]: json.loads(l) for l in open(path)} if os.path.exists(path) else {}


def is_none(a):
    return a is None or a.strip().strip(".").lower() in ("none", "", "n/a")


class Judge:
    def __init__(self):
        self.cache = {}
        if os.path.exists(JUDGE):
            for l in open(JUDGE):
                r = json.loads(l)
                self.cache[r["k"]] = r["same"]

    @staticmethod
    def key(a, b):
        return hashlib.sha1(f"{a}\x00{b}".encode()).hexdigest()

    def run(self, pairs):
        todo = [(a, b) for a, b in pairs if self.key(a, b) not in self.cache]
        for i in range(0, len(todo), 20):
            batch = todo[i:i + 20]
            text = ["Do not use any tools. For each pair, answer yes if question B asks for the same thing as "
                    "question A (same subject, same thing wanted; wording may differ), otherwise no.", ""]
            for n, (a, b) in enumerate(batch):
                text.append(f"=== PAIR {n} ===\nA: {a}\nB: {b}\n")
            text.append('Reply with only a JSON array: [{"id": 0, "answer": "yes"}, ...]')
            got = {int(o["id"]): str(o.get("answer", "")).lower().startswith("y")
                   for o in parse_list(haiku("\n".join(text))) if isinstance(o, dict) and "id" in o}
            with open(JUDGE, "a") as f:
                for n, (a, b) in enumerate(batch):
                    if n in got:
                        self.cache[self.key(a, b)] = got[n]
                        f.write(json.dumps({"k": self.key(a, b), "same": got[n]}) + "\n")

    def same(self, a, b):
        return self.cache.get(self.key(a, b))


def main():
    out_path = None
    if "--out" in sys.argv:
        out_path = sys.argv[sys.argv.index("--out") + 1]
    items = {j: {json.loads(l)["id"]: json.loads(l) for l in open(f"{DATA}/sets/items-{j}.jsonl")} for j in JOBS}
    gold = {j: load(f"{DATA}/gold/{j}.jsonl") for j in JOBS}
    runs_dir = DATA + "/runs"
    models = sorted(os.listdir(runs_dir)) if os.path.exists(runs_dir) else []
    runs = {m: {j: load(f"{runs_dir}/{m}/{j}.jsonl") for j in JOBS} for m in models}
    judge = Judge()
    # Written jobs: reference text per item (human rewrite when QReCC has one).
    ref = {}
    for j in ("rewrite", "reword"):
        for i, it in items[j].items():
            g = gold[j].get(i, {}).get("answer")
            ref[(j, i)] = it.get("key") or g
    pairs = set()
    for m in models:
        for j in ("rewrite", "reword"):
            for i, r in runs[m][j].items():
                a, b = ref.get((j, i)), r.get("answer")
                if a and not is_none(a) and b and not is_none(b):
                    pairs.add((a, b))
    judge.run(sorted(pairs))

    def right(j, i, ans):
        if j in ("rewrite", "reword"):
            a = ref.get((j, i))
            if a is None:
                return None
            if is_none(a):
                return is_none(ans)
            if is_none(ans):
                return False
            return bool(judge.same(a, ans))
        g = gold[j].get(i, {}).get("answer")
        return None if g is None else ans == g

    lines = [f"# Round 1 results ({time.strftime('%Y-%m-%d %H:%M')})", "",
             "Right = same answer as Haiku (written jobs: Haiku judges same meaning). "
             "Bad or missing replies count as wrong. Seconds are the median per item on the GPU.", ""]
    # How firm is the foundation?
    lines.append("## Haiku against trusted keys")
    for j in ("article", "checker"):
        ks = [(i, it["key"]) for i, it in items[j].items() if it.get("key") and i in gold[j]]
        if ks:
            agree = sum(gold[j][i]["answer"] == k for i, k in ks)
            lines.append(f"- {j}: Haiku agrees with the trusted key on {agree} of {len(ks)} ({100 * agree / len(ks):.0f}%)")
    qr = [(i, it["key"]) for i, it in items["rewrite"].items() if it.get("key") and i in gold["rewrite"]]
    if qr:
        judge.run([(k, gold["rewrite"][i]["answer"]) for i, k in qr])
        same = sum(bool(judge.same(k, gold["rewrite"][i]["answer"])) for i, k in qr)
        lines.append(f"- rewrite: Haiku's rewrite means the same as QReCC's human rewrite on {same} of {len(qr)}")
    lines.append("")
    for j in JOBS:
        ids = [i for i in items[j] if right(j, i, None) is not None or j in ("rewrite", "reword")]
        ids = [i for i in ids if (j in ("rewrite", "reword") and ref.get((j, i))) or i in gold[j]]
        if not ids:
            continue
        lines.append(f"## {j} ({len(ids)} items)")
        lines.append("| who | right | of all | valid | seconds |")
        lines.append("|---|---|---|---|---|")
        if j not in ("rewrite", "reword"):
            common = collections.Counter(gold[j][i]["answer"] for i in ids).most_common(1)[0]
            lines.append(f"| baseline: always '{common[0]}' | {common[1]} | {100 * common[1] / len(ids):.0f}% | | |")
        if j == "article":
            n = sum(items[j][i]["meta"]["unllm_first"] == gold[j][i]["answer"] for i in ids)
            lines.append(f"| baseline: unllm's first article | {n} | {100 * n / len(ids):.0f}% | | |")
        if j == "passage":
            n = sum(gold[j][i]["answer"] == "1" for i in ids)
            lines.append(f"| baseline: unllm's first passage | {n} | {100 * n / len(ids):.0f}% | | |")
        rows = []
        for m in models:
            rs = runs[m][j]
            if not rs:
                continue
            got = [right(j, i, rs[i]["answer"]) if i in rs and rs[i].get("valid") else False for i in ids]
            n = sum(1 for g in got if g)
            valid = sum(1 for i in ids if i in rs and rs[i].get("valid"))
            secs = [rs[i]["seconds"] for i in ids if i in rs]
            done = sum(1 for i in ids if i in rs)
            note = "" if done == len(ids) else f" ({done} done)"
            rows.append((n, f"| {m}{note} | {n} | {100 * n / len(ids):.0f}% | {100 * valid / max(1, done):.0f}% | "
                            f"{statistics.median(secs) if secs else 0:.2f} |"))
        lines += [r for _, r in sorted(rows, key=lambda x: -x[0])]
        lines.append("")
    text = "\n".join(lines)
    print(text)
    if out_path:
        with open(out_path, "w") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
