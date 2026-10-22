#!/usr/bin/env python3
"""Engine-only search vs unllm's current search, on the Simple pack, verified 50 questions.
Engine side uses only SQLite FTS5's built-in features: bm25 on passage text, bm25 on the
article title (weight 3), and a NEAR(10) proximity bonus. No word lists, no question rules.
Measure: the verified answer pattern in the top passage, and in any of the top 5."""

import os
import re
import sqlite3
import subprocess

SIMPLE = os.path.expanduser("~/.local/share/unllm/packs/wikipedia-simple/shard-000.sqlite")
PACKDIR = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/vec/packs-wikipedia-simple")
SET = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit/dev50.tsv")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
OLD = "kinds,fact,super,roman,phrase,named,howmany,howto,of,rerank,leadfirst,attr,qa,defs,floor,union,floorwide,song"


def engine(con, q, k=5):
    words = re.findall(r"[a-z0-9]+", q.lower())
    any_q = " OR ".join(f'"{w}"' for w in words)
    near_q = f"NEAR({' '.join(chr(34) + w + chr(34) for w in words)}, 10)"
    title = {d: s for d, s in con.execute(
        "SELECT rowid, bm25(titles_fts) FROM titles_fts WHERE titles_fts MATCH ? ORDER BY rank LIMIT 500", (any_q,))}
    near = {p for (p,) in con.execute("SELECT rowid FROM passages_fts WHERE passages_fts MATCH ? LIMIT 500", (near_q,))}
    rows = con.execute(
        "SELECT p.pid, p.doc_id, bm25(passages_fts), d.title, p.text FROM passages_fts f "
        "JOIN passages p ON p.pid = f.rowid JOIN docs d ON d.doc_id = p.doc_id "
        "WHERE passages_fts MATCH ? ORDER BY rank LIMIT 2000", (any_q,)).fetchall()
    # bm25() in FTS5 is negative: lower is better.
    scored = sorted(rows, key=lambda r: r[2] + 3 * title.get(r[1], 0.0) + (-5.0 if r[0] in near else 0.0))
    return [f"{r[3]}: {r[4]}" for r in scored[:k]]


def unllm(q, k=5):
    env = dict(os.environ, UNLLM_PACKS=PACKDIR, UNLLM_LAYERS=OLD)
    out = subprocess.run([CLI, "--passages", str(k), q], capture_output=True, text=True, env=env, timeout=120).stdout
    return [l[10:] for l in out.splitlines() if l.startswith("[passage] ")][:k]


def main():
    con = sqlite3.connect(f"file:{SIMPLE}?mode=ro", uri=True)
    rows = [l.split("\t") for l in open(SET).read().splitlines()[1:]]
    tot = {"unllm": [0, 0], "engine": [0, 0]}
    for qid, q, pat, typ, src in rows:
        for name, ps in (("unllm", unllm(q)), ("engine", engine(con, q))):
            top = bool(ps) and re.search(pat, ps[0], re.I) is not None
            any5 = any(re.search(pat, p, re.I) for p in ps)
            tot[name][0] += top
            tot[name][1] += any5
    for name, (t1, t5) in tot.items():
        print(f"{name:7s} answer in top passage {t1} of 50, in top 5 {t5} of 50")


if __name__ == "__main__":
    main()
