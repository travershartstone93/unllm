#!/usr/bin/env python3
"""Spine audit step 2b: could a plain full-text query have fetched a passage holding the
answer? For each of the 400 pilot questions, straight SQLite FTS5 on the Simple shard:
  strict: every content word required, top 200 by bm25
  loose:  only the three rarest content words required, top 200 by bm25
Records, per question, whether a holder (whole-word match on the keys) is in each list and
at what rank. Output DATA/audit/fts-probe.jsonl; joined with a run's F20 by spine_audit.
"""

import json
import os
import re
import sqlite3
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from taskprogress import Task  # noqa: E402

OUT = f"{DATA}/audit/fts-probe.jsonl"
SQL = ("SELECT p.text FROM passages_fts f JOIN passages p ON p.pid = f.rowid "
       "WHERE passages_fts MATCH ? ORDER BY bm25(passages_fts) LIMIT 200")


def df(con, word, cache={}):
    if word not in cache:
        row = con.execute("SELECT df FROM vocab WHERE term = ?", (word,)).fetchone()
        cache[word] = row[0] if row else 0
    return cache[word]


def holder_rank(con, words, aliases):
    if not words:
        return None
    q = " AND ".join(f'"{w}"' for w in words)
    try:
        rows = con.execute(SQL, (q,)).fetchall()
    except sqlite3.OperationalError:
        return None
    for k, (text,) in enumerate(rows):
        if nq_right(text, aliases):
            return k
    return -1  # searched, no holder in the top 200


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    con = vec_pilot.shard()
    has_vocab = con.execute("SELECT name FROM sqlite_master WHERE name = 'vocab'").fetchone() is not None
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    done = {r["id"] for r in vec_pilot.load(OUT)}
    bar = Task("fts-probe", "Spine audit: first-stage probe", total=len(items))
    bar.skip(len(done))
    with open(OUT, "a") as f:
        for it in items:
            if it["id"] in done:
                continue
            words = vec_pilot.content(it["question"])
            t = time.time()
            strict = holder_rank(con, words, it["aliases"])
            rare = sorted(words, key=lambda w: df(con, w) if has_vocab else len(w))[:3] if has_vocab else words[:3]
            loose = holder_rank(con, rare, it["aliases"]) if rare != words else strict
            f.write(json.dumps({"id": it["id"], "words": words, "strict": strict, "rare": rare, "loose": loose,
                                "ms": int(1000 * (time.time() - t))}) + "\n")
            f.flush()
            bar.step()
    bar.finish()
    rows = vec_pilot.load(OUT)
    s_hit = sum(1 for r in rows if r["strict"] is not None and r["strict"] >= 0)
    l_hit = sum(1 for r in rows if r["loose"] is not None and r["loose"] >= 0)
    s20 = sum(1 for r in rows if r["strict"] is not None and 0 <= r["strict"] < 20)
    print(f"strict (all words): holder in top 200 for {s_hit} of {len(rows)} (in top 20: {s20}); "
          f"loose (3 rarest): {l_hit}; median ms {sorted(r['ms'] for r in rows)[len(rows) // 2]}")


if __name__ == "__main__":
    main()
