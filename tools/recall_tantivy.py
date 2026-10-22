#!/usr/bin/env python3
"""Plain Tantivy search over the whole library, for comparison with unllm's
answering: the question's words (minus stop words) through
`unllm_pack search`, best 20 passages across every indexed pack, no unllm
rules. Same questions, sample and scoring as recall.py and recall_plain.py.

  recall_tantivy.py [--n 300] [--seed 11] [--limit N] [--packs DIR]
"""

import argparse
import json
import os
import random
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recall import CUTS, cool_down, normalize  # noqa: E402
from recall_plain import words  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=300, help="sample size (same draw as recall.py)")
    ap.add_argument("--limit", type=int, default=0, help="only the first N of the sample")
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--packs", default=os.path.expanduser("~/.local/share/unllm/packs"))
    ap.add_argument("--tool", default=os.path.join(HERE, "..", "build", "unllm_pack"))
    ap.add_argument("--nq-file", default=os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet"))
    ap.add_argument("--out", default=os.path.expanduser("~/Downloads/unllm-corpus-sources/reports"))
    args = ap.parse_args()
    import pyarrow.parquet as pq
    rows = pq.read_table(args.nq_file).to_pylist()
    sample = random.Random(args.seed).sample(rows, args.n)
    if args.limit:
        sample = sample[:args.limit]
    n = len(sample)
    stamp = time.strftime("%Y-%m-%d-%H%M") + f"-recall-tantivy-n{n}"
    jpath = os.path.join(args.out, stamp + ".jsonl")
    proc = subprocess.Popen([args.tool, "search", args.packs, "-", "20"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True, bufsize=1)
    assert proc.stdin and proc.stdout
    hits = {k: 0 for k in CUTS}
    times, packs_hit = [], {}
    with open(jpath, "w") as jf:
        for i, r in enumerate(sample, 1):
            cool_down()
            proc.stdin.write(" ".join(words(r["question"])) + "\n")
            proc.stdin.flush()
            found = []
            while True:
                line = proc.stdout.readline()
                if not line:
                    sys.exit("unllm_pack stopped")
                parts = line.rstrip("\n").split("\t")
                if parts[0] == "---":
                    times.append(float(parts[1]))
                    break
                found.append(parts)
            aliases = [normalize(a) for a in r["answer"] if normalize(a)]
            first = None
            for rank, (pack, _, text) in enumerate(found[:20], 1):
                if any(a in normalize(text) for a in aliases):
                    first = rank
                    packs_hit[pack] = packs_hit.get(pack, 0) + 1
                    break
            for k in CUTS:
                hits[k] += 1 if first is not None and first <= k else 0
            jf.write(json.dumps({"question": r["question"], "answers": r["answer"], "first_hit": first,
                                 "ms": times[-1], "top": [p[:2] for p in found[:5]]}) + "\n")
            jf.flush()
            if i % 25 == 0:
                print(f"[{i}/{n}] " + "  ".join(f"top {k}: {100 * hits[k] / i:.0f}%" for k in CUTS), flush=True)
    proc.stdin.close()
    proc.wait()
    times.sort()
    text = "\n".join([f"plain Tantivy search (all packs, OR of question words, bm25) on {n} Natural Questions "
                      f"(seed {args.seed}), {time.strftime('%Y-%m-%d %H:%M')}",
                      "answer found in the first passages:"] +
                     [f"  top {k:<2}  {hits[k]:>4} of {n}  ({100 * hits[k] / n:.0f}%)" for k in CUTS] +
                     [f"first hit came from: " + ", ".join(f"{p} {c}" for p, c in sorted(packs_hit.items(),
                                                                                       key=lambda x: -x[1])),
                      f"time per question: median {times[n // 2]:.0f} ms, slowest {times[-1]:.0f} ms"])
    print("\n" + text)
    with open(os.path.join(args.out, stamp + ".txt"), "w") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    sys.exit(main())
