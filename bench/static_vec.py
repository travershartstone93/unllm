#!/usr/bin/env python3
"""Route A of the spine audit: can the library's own static word vectors (potion-base-8M, the
model behind its meaning score, no neural network at ask time) do the vector search?
Embeds every passage of the Simple shard and the 400 pilot questions as the normalised
average of word vectors, searches by cosine, keeps the top 20 (one per article), scores
holder-first / top-5 / top-20 like the pilot. Marks written first: holder first >= 130 and
top-20 >= 250 (word search 92 / 195, bge-small 159 / 290). Output DATA/vec/static.jsonl.
"""

import json
import os
import re
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from qa_score import nq_right  # noqa: E402
from taskprogress import Task  # noqa: E402

MODEL = os.path.expanduser("~/Downloads/unllm-models/potion-base-8M")
OUT = f"{vec_pilot.VEC}/static.jsonl"
CHUNK = 50_000
K = 60  # raw neighbours kept before the one-per-article cut to 20


def one_per_article(ps):
    seen, out = set(), []
    for p in ps:
        t = p.split(": ", 1)[0]
        if t not in seen:
            seen.add(t)
            out.append(p)
    return out


def main():
    from model2vec import StaticModel
    model = StaticModel.from_pretrained(MODEL)
    con = vec_pilot.shard()
    total = con.execute("SELECT count(*) FROM passages").fetchone()[0]
    bar = Task("static-vec", "Static vectors: Simple shard", total=total + 1)
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    q = model.encode([vec_pilot.QUERY_PREFIX + it["question"] for it in items]).astype(np.float32)
    q /= np.linalg.norm(q, axis=1, keepdims=True) + 1e-9
    best_s = np.full((len(items), K), -2.0, dtype=np.float32)
    best_p = np.zeros((len(items), K), dtype=np.int64)
    t0 = time.time()
    rows = con.execute("SELECT p.pid, d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id ORDER BY p.pid")
    done = 0
    while True:
        chunk = rows.fetchmany(CHUNK)
        if not chunk:
            break
        pids = np.array([r[0] for r in chunk], dtype=np.int64)
        v = model.encode([f"{t}: {' '.join(x.split())[:900]}" for _, t, x in chunk]).astype(np.float32)
        v /= np.linalg.norm(v, axis=1, keepdims=True) + 1e-9
        s = q @ v.T  # (400, chunk)
        cand_s = np.concatenate([best_s, s], axis=1)
        cand_p = np.concatenate([best_p, np.broadcast_to(pids, (len(items), len(pids)))], axis=1)
        idx = np.argpartition(-cand_s, K - 1, axis=1)[:, :K]
        best_s = np.take_along_axis(cand_s, idx, axis=1)
        best_p = np.take_along_axis(cand_p, idx, axis=1)
        done += len(chunk)
        bar.step(len(chunk), detail=f"{done / (time.time() - t0):,.0f}/s")
    r1 = r5 = r20 = 0
    with open(OUT, "w") as f:
        for i, it in enumerate(items):
            order = np.argsort(-best_s[i])
            ps = []
            for pid in best_p[i][order]:
                row = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid = ?",
                                  (int(pid),)).fetchone()
                if row:
                    ps.append(f"{row[0]}: {' '.join(row[1].split())}")
            ps = one_per_article(ps)[:20]
            pos = next((k for k, p in enumerate(ps) if nq_right(p, it["aliases"])), -1)
            r1 += pos == 0
            r5 += 0 <= pos < 5
            r20 += pos >= 0
            f.write(json.dumps({"id": it["id"], "passages": ps}) + "\n")
    bar.finish()
    line = (f"static word vectors (potion-base-8M): holder first {r1}, in top 5 {r5}, in top 20 {r20} of {len(items)}; "
            f"{total:,} passages embedded in {(time.time() - t0) / 60:.1f} min; "
            f"{'PASS' if r1 >= 130 and r20 >= 250 else 'FAIL'} (marks 130 / 250; word 92 / 195, bge-small 159 / 290)")
    print(line)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n### Route A, static word vectors as the search (2026-10-19)\n- " + line + "\n")


if __name__ == "__main__":
    main()
