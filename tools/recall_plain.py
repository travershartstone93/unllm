#!/usr/bin/env python3
"""Plain keyword search, for comparison with unllm's: the question's words
(minus stop words) searched as one OR query over the English Wikipedia pack,
ranked by bm25, best 20 passages across all shards. No unllm rules at all.
Same questions, sample and scoring as recall.py.

  recall_plain.py [--n 300] [--seed 11] [--pack wikipedia-en]
"""

import argparse
import concurrent.futures
import glob
import json
import os
import random
import re
import sqlite3
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recall import CUTS, cool_down, normalize  # noqa: E402

STOP = set("""a an the of in on at to for from by with and or is are was were be been being what which who whom whose
when where why how do does did has have had it its this that these those as into than then there their them they he
she his her i you we our your my me us not no can could would should will shall may might must about after before
over under up down out off so if but also just only very more most many much some any all each both few other such
own same too s t don now""".split())


def words(q):
    return [w for w in re.findall(r"[a-z0-9]+", q.lower()) if w not in STOP]


def search_shard(path, match, k):
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        return db.execute("SELECT bm25(passages_fts), p.text FROM passages_fts f JOIN passages p ON p.pid = f.rowid "
                          "WHERE passages_fts MATCH ? ORDER BY bm25(passages_fts) LIMIT ?", (match, k)).fetchall()
    except sqlite3.Error:
        return []
    finally:
        db.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=300, help="sample size (same draw as recall.py)")
    ap.add_argument("--limit", type=int, default=0, help="only the first N of the sample")
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--pack", default="wikipedia-en")
    ap.add_argument("--nq-file", default=os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet"))
    ap.add_argument("--out", default=os.path.expanduser("~/Downloads/unllm-corpus-sources/reports"))
    args = ap.parse_args()
    import pyarrow.parquet as pq
    rows = pq.read_table(args.nq_file).to_pylist()
    sample = random.Random(args.seed).sample(rows, args.n)
    if args.limit:
        sample = sample[:args.limit]
    args.n = len(sample)
    shards = sorted(glob.glob(os.path.expanduser(f"~/.local/share/unllm/packs/{args.pack}/shard-*.sqlite")))
    stamp = time.strftime("%Y-%m-%d-%H%M") + f"-recall-plain-n{args.n}"
    jpath = os.path.join(args.out, stamp + ".jsonl")
    hits = {k: 0 for k in CUTS}
    times = []
    with open(jpath, "w") as jf, concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        for i, r in enumerate(sample, 1):
            cool_down()
            match = " OR ".join(f'"{w}"' for w in words(r["question"]))
            t = time.time()
            found = []
            if match:
                for part in ex.map(lambda s: search_shard(s, match, 20), shards):
                    found.extend(part)
            found.sort(key=lambda x: x[0])  # bm25: lower is better
            times.append(time.time() - t)
            aliases = [normalize(a) for a in r["answer"] if normalize(a)]
            first = None
            for rank, (_, text) in enumerate(found[:20], 1):
                if any(a in normalize(text) for a in aliases):
                    first = rank
                    break
            for k in CUTS:
                hits[k] += 1 if first is not None and first <= k else 0
            jf.write(json.dumps({"question": r["question"], "answers": r["answer"], "first_hit": first,
                                 "seconds": round(times[-1], 2)}) + "\n")
            jf.flush()
            if i % 25 == 0:
                print(f"[{i}/{args.n}] " + "  ".join(f"top {k}: {100 * hits[k] / i:.0f}%" for k in CUTS), flush=True)
    n = args.n
    times.sort()
    text = "\n".join([f"plain keyword search ({args.pack}, OR of question words, bm25) on {n} Natural Questions "
                      f"(seed {args.seed}), {time.strftime('%Y-%m-%d %H:%M')}",
                      "answer found in the first passages:"] +
                     [f"  top {k:<2}  {hits[k]:>4} of {n}  ({100 * hits[k] / n:.0f}%)" for k in CUTS] +
                     [f"time per question: median {times[n // 2]:.1f} s, slowest {times[-1]:.1f} s"])
    print("\n" + text)
    with open(os.path.join(args.out, stamp + ".txt"), "w") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    sys.exit(main())
