#!/usr/bin/env python3
"""Step 3: which reranker to port. Every reranker orders the same saved pool (word top 25 + vector
top 25, from pool_read50.py save with POOL_TAG) for each question; measured: the answer in the
reranked top 1 / top 3 / top 10, and seconds per question on the GPU.
  rerank_cmp.py TAG NAME [NAME ...]     names from rerank.MODELS
Rows -> DATA/audit/rerank-cmp-TAG-NAME.jsonl; totals to the audit report."""
import json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import torch  # noqa: E402
import rerank  # noqa: E402
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md")


def main(tag, names):
    rows = [json.loads(l) for l in open(f"{A}/pool-top10-{tag}.jsonl")]
    sets = sorted({r["set"] for r in rows})
    hit = lambda pat, t: re.search(pat, t, re.I) is not None  # noqa: E731
    lines = [f"\n### Step 3, rerankers on the same saved pool ({tag}: {', '.join(sets)}) ({time.strftime('%Y-%m-%d %H:%M')})"]
    for name in names:
        ranker = rerank.load(name)
        tot = {s: {"top 1": 0, "top 3": 0, "top 10": 0} for s in sets}
        secs = 0.0
        out = open(f"{A}/rerank-cmp-{tag}-{name}.jsonl", "w")
        bar = Task("rerankcmp", f"Step 3: {name} reranks {len(rows)} pools", total=len(rows))
        for r in rows:
            pool = r["pool"]
            t0 = time.time()
            sc = [float(x) for x in ranker.scores(r["question"], pool)] if pool else []
            secs += time.time() - t0
            rr = [pool[k] for k in sorted(range(len(pool)), key=lambda k: -sc[k])]
            res = {f"top {n}": any(hit(r["pattern"], p) for p in rr[:n]) for n in (1, 3, 10)}
            for k, v in res.items():
                tot[r["set"]][k] += v
            out.write(json.dumps({"set": r["set"], "id": r["id"], **res, "top3": rr[:3]}) + "\n")
            bar.step(detail=f"{r['set']} {r['id']}")
        bar.finish()
        for s in sets:
            lines.append(f"- {name}, {s}: " + ", ".join(f"{k} {v}" for k, v in tot[s].items()) + " of 50")
        lines.append(f"- {name}: {secs / len(rows):.2f} s per question on the GPU, {sum(len(r['pool']) for r in rows) / len(rows):.0f} passages each")
        del ranker
        torch.cuda.empty_cache()
    print("\n".join(lines))
    with open(REPORT, "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
