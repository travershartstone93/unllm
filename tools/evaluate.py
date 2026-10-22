#!/usr/bin/env python3
"""The honest scoreboard: how often unllm is right when it answers
(precision), how often it answers at all (coverage), and how often it makes
something up when it should say it does not know.

  evaluate.py nq [--n 300] [--seed 11] [--avoid-seed S]   Natural Questions sample
  evaluate.py tsv FILE [FILE ...]                          question files

Question files are tab separated with a header: a "question" column and an
answer column ("answer_pattern" or "expect", a regex). A file without an
answer column (unknowable.tsv) holds questions with no knowable answer:
there the right reply is to decline, and any answer counts as made up.
Files whose name contains "sealed" or "heldout" only ever report totals: no per-question
lines are written or printed.

Every reply is one of: answered, hedged ("The closest thing I know..."),
or abstained. Precision = right / (answered + hedged); the confident
precision leaves hedged replies out. Natural Questions answers are matched
as whole words (tools/qa_score.py).

Options: --layers L (UNLLM_LAYERS), --workers N (default 1: the search has
time limits, so parallel runs are less repeatable), --label TEXT, --out DIR.
Saves per question as it goes and resumes a stopped run; shows a progress
bar in the system tray; pauses above 85 C.
"""

import argparse
import concurrent.futures
import csv
import json
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qa_score import nq_right, outcome, pattern_right, run_unllm  # noqa: E402
from recall import REPO, cool_down  # noqa: E402
from taskprogress import Task  # noqa: E402


def nq_items(n, seed, avoid):
    import pyarrow.parquet as pq
    rows = pq.read_table(os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet")).to_pylist()
    if avoid is not None:
        taken = {r["question"] for r in random.Random(avoid).sample(rows, n)}
        rows = [r for r in rows if r["question"] not in taken]
    return [{"id": f"nq{i:03d}", "question": r["question"], "aliases": r["answer"]}
            for i, r in enumerate(random.Random(seed).sample(rows, n))]


def tsv_items(path):
    with open(path) as f:
        rows = [line for line in f if not line.startswith("#")]
    out = []
    for i, r in enumerate(csv.DictReader(rows, delimiter="\t")):
        pattern = r.get("answer_pattern") or r.get("expect")
        out.append({"id": r.get("id") or f"q{i:03d}", "question": r["question"], "pattern": pattern,
                    "unknowable": pattern is None or r.get("category") == "limits"})
    return out


def judge(item, reply):
    kind = outcome(reply["branch"], reply["answer"])
    if item.get("unknowable"):
        return kind, kind == "abstained"
    if kind == "abstained":
        return kind, False
    if "aliases" in item:
        return kind, nq_right(reply["answer"], item["aliases"])
    return kind, pattern_right(reply["answer"], item["pattern"])


def summarize(name, results):
    lines = []
    unk = [r for r in results if r.get("unknowable")]
    results = [r for r in results if not r.get("unknowable")]
    if unk:
        declined = sum(1 for r in unk if r["kind"] == "abstained")
        lines.append(f"{name}: {len(unk)} unknowable questions, declined {declined}, made up {len(unk) - declined}")
    if not results:
        return "\n".join(lines)
    n = len(results)
    c = {k: sum(1 for r in results if r["kind"] == k) for k in ("answered", "hedged", "abstained")}
    right = sum(1 for r in results if r["right"])
    right_conf = sum(1 for r in results if r["right"] and r["kind"] == "answered")
    replied = c["answered"] + c["hedged"]
    pct = lambda a, b: f"{100 * a / b:.0f}%" if b else "n/a"  # noqa: E731
    return "\n".join([(f"{name}: {n} questions\n"
            f"  right {right} ({pct(right, n)} of all)   answered {c['answered']}   hedged {c['hedged']}   "
            f"declined {c['abstained']}\n"
            f"  precision {pct(right, replied)} (right / replied)   confident precision "
            f"{pct(right_conf, c['answered'])} (hedged left out)   coverage {pct(replied, n)}")] + lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("set", choices=["nq", "tsv"])
    ap.add_argument("files", nargs="*")
    ap.add_argument("--n", type=int, default=300)
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--avoid-seed", type=int)
    ap.add_argument("--layers")
    ap.add_argument("--workers", type=int, default=1)
    ap.add_argument("--label", default="")
    ap.add_argument("--unllm", default=os.path.join(REPO, "build", "unllm"))
    ap.add_argument("--out", default=os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/eval"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    tag = (args.label + "-") if args.label else ""
    if args.set == "nq":
        sets = [(f"nq-seed{args.seed}" + (f"-not{args.avoid_seed}" if args.avoid_seed is not None else "") +
                 f"-n{args.n}", nq_items(args.n, args.seed, args.avoid_seed))]
    else:
        sets = [(os.path.splitext(os.path.basename(f))[0], tsv_items(f)) for f in args.files]
    total = sum(len(items) for _, items in sets)
    bar = Task("evaluate", f"Evaluate {args.label or 'unllm'}", total=total)
    lines = []
    for name, items in sets:
        sealed = "sealed" in name or "heldout" in name
        path = os.path.join(args.out, f"{tag}{name}.jsonl")
        done = {}
        if not sealed and os.path.exists(path):
            with open(path) as f:
                for line in f:
                    r = json.loads(line)
                    done[r["id"]] = r
        bar.step(len(done), detail=name)
        todo = [it for it in items if it["id"] not in done]
        results = list(done.values())
        jf = None if sealed else open(path, "a")

        def one(it):
            cool_down()
            reply = run_unllm(args.unllm, it["question"], args.layers)
            kind, right = judge(it, reply)
            return {"id": it["id"], "question": it["question"], "kind": kind, "right": right,
                    "unknowable": bool(it.get("unknowable")),
                    "branch": reply["branch"], "answer": reply["answer"][:400]}
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
            for r in ex.map(one, todo):
                results.append(r)
                if jf:
                    jf.write(json.dumps(r) + "\n")
                    jf.flush()
                bar.step(detail=name)
        if jf:
            jf.close()
        if sealed:  # totals only, kept apart from everything else
            results = [{"kind": r["kind"], "right": r["right"], "unknowable": r["unknowable"]} for r in results]
        lines.append(summarize(name, results))
    bar.finish()
    head = (f"unllm evaluation {time.strftime('%Y-%m-%d %H:%M')}"
            f"{' layers=' + args.layers if args.layers is not None else ''}{' (' + args.label + ')' if args.label else ''}")
    text = "\n".join([head] + lines)
    print(text)
    with open(os.path.join(args.out, f"{tag}{time.strftime('%Y-%m-%d-%H%M%S')}-summary.txt"), "w") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    sys.exit(main())
