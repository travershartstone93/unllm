#!/usr/bin/env python3
"""Spine audit analysis over spine_audit.py runs.

  spine_split.py picker NAME      NAME ran with --debug and the picker on: splits the
                                  in-hand losses by what the picker's trace shows, and writes
                                  the wrong commits to DATA/audit/NAME-wrong.md for reading
  spine_split.py search BASE FULL BASE is the baseline run, FULL a --top run (whole ranked
                                  list): splits BASE's not-in-hand questions into fetched but
                                  dropped / never fetched, with the FTS probe's columns

Debug lines (answer.cpp): "  span i: SCORE KEY [supported] pattern P "TYPE"" for the picker's
top candidates, best first; "  rank k: ..." for the reranked passages.
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from spine_audit import AUDIT, CLOSEST, REPORT  # noqa: E402

SPAN = re.compile(r'^\s*span (\d+): ([\d.]+) (.*?)( supported)? pattern (\d+) "([^"]*)"\s*$')


def items():
    return {it["id"]: it for it in vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")}


def spans(debug):
    out = []
    for line in debug.splitlines():
        m = SPAN.match(line)
        if m:
            out.append({"key": m.group(3), "supported": bool(m.group(4)), "pattern": int(m.group(5)), "type": m.group(6)})
    return out


def picker(name):
    its = items()
    rows = vec_pilot.load(f"{AUDIT}/{name}.jsonl")
    c = {"right": 0, "in hand": 0, "gate refused a right best": 0, "right candidate not best": 0,
         "no right candidate": 0, "picker did not run": 0, "wrong commit": 0}
    wrong = []
    for r in rows:
        al = its[r["id"]]["aliases"]
        ok = bool(r["answer"]) and nq_right(r["answer"], al)
        found = any(nq_right(p, al) for p in r["passages"][:20])
        c["right"] += ok
        if ok or not found:
            continue
        c["in hand"] += 1
        sp = spans(r["debug"])
        hedged = r["answer"].startswith(CLOSEST)
        if not sp:
            c["picker did not run"] += 1
            continue
        rights = [s for s in sp if nq_right(s["key"], al)]
        if hedged or not r["answer"]:
            if rights and rights[0] is sp[0]:
                c["gate refused a right best"] += 1
            elif rights:
                c["right candidate not best"] += 1
            else:
                c["no right candidate"] += 1
        else:
            c["wrong commit"] += 1
            wrong.append((r, sp, rights))
    lines = [f"\n### Picker split for run {name} ({len(rows)} questions)"] + [f"- {k}: {v}" for k, v in c.items()]
    text = "\n".join(lines)
    print(text)
    with open(REPORT, "a") as f:
        f.write(text + "\n")
    with open(f"{AUDIT}/{name}-wrong.md", "w") as f:
        f.write(f"# Wrong commits in run {name}: {len(wrong)}\n\n")
        for r, sp, rights in wrong:
            it = its[r["id"]]
            f.write(f"## {r['id']}: {it['question']}\nkey: {it['aliases'][:4]}\nanswer: {r['answer'][:200]}\n")
            f.write("candidates: " + "; ".join(f"{s['key']}{' (sup)' if s['supported'] else ''} p{s['pattern']}" for s in sp[:5]))
            f.write(f"\nright candidate shown: {'yes' if rights else 'no'}; type: {sp[0]['type']}\n\n")
    print(f"wrong commits written to {AUDIT}/{name}-wrong.md")


def search(base, full):
    its = items()
    b = {r["id"]: r for r in vec_pilot.load(f"{AUDIT}/{base}.jsonl")}
    w = {r["id"]: r for r in vec_pilot.load(f"{AUDIT}/{full}.jsonl")}
    probe = {r["id"]: r for r in vec_pilot.load(f"{AUDIT}/fts-probe.jsonl")}
    c = {"not in hand": 0, "fetched by the wide list but dropped": 0, "never fetched": 0,
         "never fetched, strict FTS top 200 has it": 0, "never fetched, loose FTS top 200 has it": 0}
    depth = []
    for i, r in b.items():
        al = its[i]["aliases"]
        if any(nq_right(p, al) for p in r["passages"][:20]):
            continue
        c["not in hand"] += 1
        ps = w.get(i, {}).get("passages", [])
        pos = next((k for k, p in enumerate(ps) if nq_right(p, al)), -1)
        if pos >= 0:
            c["fetched by the wide list but dropped"] += 1
            depth.append(pos + 1)
        else:
            c["never fetched"] += 1
            pr = probe.get(i, {})
            if pr.get("strict") is not None and pr["strict"] >= 0:
                c["never fetched, strict FTS top 200 has it"] += 1
            if pr.get("loose") is not None and pr["loose"] >= 0:
                c["never fetched, loose FTS top 200 has it"] += 1
    depth.sort()
    lines = [f"\n### Search split: {base} not-in-hand against the whole list of {full}"] + [f"- {k}: {v}" for k, v in c.items()]
    if depth:
        lines.append(f"- where the wide list held it: median position {depth[len(depth) // 2]}, worst {depth[-1]}")
    text = "\n".join(lines)
    print(text)
    with open(REPORT, "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "picker":
        picker(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "search":
        search(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
