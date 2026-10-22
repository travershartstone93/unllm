#!/usr/bin/env python3
"""Spine audit: the picker's full candidate trace (a run made with UNLLM_DEBUG=2).

  spine_cands.py RUN RAWRUN      RUN: picker-on run with the full trace; RAWRUN: a --top run
                                 with the same layers minus the picker, whose raw list is the
                                 passage list the picker saw (same order)

Measured, over questions with the answer in the printed top 20:
- was a right candidate generated at all, and at what rank among the candidates
- the type the picker gave the right candidate against the question's shape
- whether the right candidate's passage was the top passage
Simulated: each candidate's score multiplied by (0.3 + 0.7 * match), match being the share of
the question's content words, weighted by rarity, present in the candidate's passage; counts
the losses that flip to right and the right answers that flip to wrong.
"""

import collections
import os
import re
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from qa_score import nq_right  # noqa: E402
from spine_audit import AUDIT, REPORT  # noqa: E402
from spine_split import SPAN, items  # noqa: E402

CAND = re.compile(r"^  cand (\d+)\t([\d.]+)\t(.*?)\t(\w+)\t([01])\t(\d+)\t(.*?)\t(.*)$")


def cands(debug):
    out = []
    for line in debug.splitlines():
        m = CAND.match(line)
        if m:
            out.append({"rank": int(m.group(1)), "score": float(m.group(2)), "key": m.group(3), "type": m.group(4),
                        "sup": m.group(5) == "1", "pattern": int(m.group(6)), "title": m.group(7), "text50": m.group(8)})
    return out


def shape(debug):
    for line in debug.splitlines():
        m = SPAN.match(line)
        if m:
            return m.group(6)
    return None


def main(run, rawrun):
    its = items()
    rows = vec_pilot.load(f"{AUDIT}/{run}.jsonl")
    raw = {r["id"]: r["passages"] for r in vec_pilot.load(f"{AUDIT}/{rawrun}.jsonl")}
    con = vec_pilot.shard()
    total_docs = con.execute("SELECT count(*) FROM passages").fetchone()[0]
    dfc = {}

    def idf(w):
        if w not in dfc:
            row = con.execute("SELECT df FROM vocab WHERE term = ?", (w,)).fetchone()
            dfc[w] = (total_docs / (1 + (row[0] if row else 0))) ** 0.5
        return dfc[w]

    c = collections.Counter()
    types = collections.Counter()
    keyforms = collections.Counter()
    flips = collections.Counter()
    for r in rows:
        it = its[r["id"]]
        al = it["aliases"]
        ps = r["passages"][:20]
        hpos = next((k for k, p in enumerate(ps) if nq_right(p, al)), -1)
        if hpos < 0:
            continue
        ok = bool(r["answer"]) and nq_right(r["answer"], al)
        cl = cands(r["debug"])
        c["in hand"] += 1
        if not cl:
            c["no candidates (no shape)"] += 1
            continue
        rights = [x for x in cl if nq_right(x["key"], al)]
        if ok:
            c["right"] += 1
        elif not rights:
            c["loss: right span never a candidate"] += 1
            key = al[0]
            form = ("number" if re.fullmatch(r"[\d,.]+", key) else "year" if re.fullmatch(r"\d{4}", key)
                    else "Capitalised phrase" if re.match(r"[A-Z]", key) and " " in key else "Capitalised word"
                    if re.match(r"[A-Z]", key) else "lowercase")
            keyforms[f"{form} (holder at rank {min(hpos + 1, 4)}{'+' if hpos >= 3 else ''})"] += 1
        else:
            rk = rights[0]["rank"]
            c[f"loss: right span generated, rank {'1' if rk == 0 else '2-5' if rk < 5 else '6-20' if rk < 20 else '21+'}"] += 1
            types[f"shape {shape(r['debug'])} -> right span typed {rights[0]['type']}"] += 1
        # Simulation: match of each candidate's passage to the question's rarer words.
        qw = [w for w in vec_pilot.content(it["question"])]
        if not qw:
            continue
        wsum = sum(idf(w) for w in qw)
        rawps = raw.get(r["id"], [])

        def match(x):
            text = next((p for p in rawps if x["text50"] and x["text50"][:30] in p), x["title"] + " " + x["text50"])
            low = (x["title"] + " " + text).lower()
            return sum(idf(w) for w in set(qw) if re.search(r"\b" + re.escape(w) + r"s?\b", low)) / wsum
        best = max(cl, key=lambda x: x["score"] * (0.3 + 0.7 * match(x)))
        new_ok = nq_right(best["key"], al)
        if ok and not new_ok:
            flips["right -> wrong"] += 1
        elif not ok and new_ok:
            flips["loss -> right (best candidate now right)"] += 1
    lines = [f"\n### Candidate trace of run {run} (questions with the answer in the printed top 20)"]
    lines += [f"- {k}: {v}" for k, v in sorted(c.items(), key=lambda x: (-x[1], x[0]))]
    lines.append("- right span never a candidate, by the key's form and the holder's rank: " +
                 "; ".join(f"{k} {v}" for k, v in keyforms.most_common()))
    lines.append("- when generated, the right span's type against the question's shape: " +
                 "; ".join(f"{k} {v}" for k, v in types.most_common(8)))
    lines.append("- simulation, scores reweighted by passage match to the question's rare words: " +
                 "; ".join(f"{k} {v}" for k, v in flips.items()))
    text = "\n".join(lines)
    print(text)
    with open(REPORT, "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
