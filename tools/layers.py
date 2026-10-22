#!/usr/bin/env python3
"""Which of unllm's answering layers help, on top of plain search.

Runs the same Natural Questions sample through `unllm` with different
UNLLM_LAYERS settings: plain search alone, every layer, and every layer but
one (once per layer). For each setting: how often unllm's answer holds an
accepted answer, and how often one is in its first 1, 5 and 20 passages.

  layers.py [--n 300] [--seed 11] [--avoid-seed S] [--only "set1;set2"]

--avoid-seed draws a second sample that shares no question with the seed-S
sample, to confirm a choice made on that one. Each setting's result is
appended to the report as soon as it is done; a rerun skips settings
already in it. Pauses while the CPU is above 85 C.
"""

import argparse
import concurrent.futures
import json
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qa_score import nq_right, outcome, run_unllm  # noqa: E402
from recall import REPO, cool_down  # noqa: E402
from taskprogress import Task  # noqa: E402

LAYERS = ["kinds", "fact", "super", "roman", "phrase", "named", "howmany", "howto", "of", "rerank", "leadfirst",
          "attr", "qa", "defs", "opening", "floor"]


def sample_of(rows, n, seed, avoid):
    if avoid is None:
        return random.Random(seed).sample(rows, n)
    taken = {r["question"] for r in random.Random(avoid).sample(rows, n)}
    rest = [r for r in rows if r["question"] not in taken]
    return random.Random(seed).sample(rest, n)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=300)
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--avoid-seed", type=int)
    ap.add_argument("--only", help="settings to run, separated by ';' (each a comma list of layers)")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--unllm", default=os.path.join(REPO, "build", "unllm"))
    ap.add_argument("--nq-file", default=os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet"))
    ap.add_argument("--out", default=os.path.expanduser("~/Downloads/unllm-corpus-sources/reports"))
    args = ap.parse_args()
    import pyarrow.parquet as pq
    rows = pq.read_table(args.nq_file).to_pylist()
    sample = sample_of(rows, args.n, args.seed, args.avoid_seed)
    tag = f"layers2-seed{args.seed}" + (f"-not{args.avoid_seed}" if args.avoid_seed is not None else "") + f"-n{args.n}"
    report = os.path.join(args.out, tag + ".tsv")
    done = set()
    if os.path.exists(report):
        with open(report) as f:
            done = {line.split("\t")[0] for line in f if line.strip() and not line.startswith("#")}
    else:
        with open(report, "w") as f:
            f.write("# setting\tanswer right\treplied\ttop1\ttop5\ttop20\tseconds\n")
    everything = ",".join(LAYERS)
    if args.only:
        settings = [(s.strip() or "plain", s.strip()) for s in args.only.split(";")]
    else:
        settings = [("plain", ""), ("all-new", everything), ("all-old", "all")]
        settings += [(f"all-new minus {x}", ",".join(y for y in LAYERS if y != x)) for x in LAYERS]
    todo = [(name, layers) for name, layers in settings if name not in done]
    bar = Task("layer-sweep", f"Layer sweep seed {args.seed}", total=len(todo) * len(sample))
    for i, (name, layers) in enumerate(todo, 1):
        bar.note(f"setting {i} of {len(todo)}: {name}")
        t = time.time()
        results = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
            futures = []
            for r in sample:
                futures.append(ex.submit(lambda r=r: (cool_down(), score_env(args.unllm, layers, r))[1]))
                futures[-1].add_done_callback(lambda _: bar.step())
            results = [f.result() for f in futures]
        n = len(results)
        right = sum(x["right"] for x in results)
        said = sum(x["said"] for x in results)
        top = {k: sum(1 for x in results if x["first"] and x["first"] <= k) for k in (1, 5, 20)}
        line = f"{name}\t{right}\t{said}\t{top[1]}\t{top[5]}\t{top[20]}\t{time.time() - t:.0f}"
        with open(report, "a") as f:
            f.write(line + "\n")
        with open(os.path.join(args.out, tag + "-" + name.replace(" ", "_") + ".jsonl"), "w") as f:
            for x in results:
                f.write(json.dumps(x) + "\n")
        print(f"{name:<28} answer right {right:>3}/{n} ({100 * right / n:.0f}%)  top1 {top[1]:>3}  top5 {top[5]:>3}  "
              f"top20 {top[20]:>3}", flush=True)
    bar.finish()
    print(f"report: {report}")


def score_env(binary, layers, r):
    reply = run_unllm(binary, r["question"], layers)
    first = None
    for rank, p in enumerate(reply["passages"][:20], 1):
        if nq_right(p, r["answer"]):
            first = rank
            break
    kind = outcome(reply["branch"], reply["answer"])
    return {"question": r["question"], "branch": reply["branch"], "answer": reply["answer"][:300], "kind": kind,
            "right": kind != "abstained" and nq_right(reply["answer"], r["answer"]), "first": first,
            "said": kind != "abstained"}


if __name__ == "__main__":
    sys.exit(main())
