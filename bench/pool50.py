#!/usr/bin/env python3
"""Search only: word search and vector search pooled, Qwen3-Reranker-0.6B picks the top 10.
Each question runs both searches (top 25 each); the reranker orders three lists from the same run:
words alone, vectors alone (full index), and both pooled (duplicates dropped). Measures whether the
answer is in the reranked top 10 and top 1. Sets: corpus50 and dev50.
Rows to DATA/audit/pool50.jsonl, totals to the audit report."""
import json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import rerank  # noqa: E402
from qa_score import run_unllm  # noqa: E402
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
FULL = os.path.expanduser("~/.local/share/unllm/vectors/wiki-full.unlpq")
K = 25
# Each search's own list, K long: no early answer steps, no word filters, no one-per-article cut.
SEARCHES = {"words": {"UNLLM_LAYERS": "union,wide,rerank", "UNLLM_TOP": str(K)},
            "vectors": {"UNLLM_LAYERS": "meaning", "UNLLM_MEANING": FULL, "UNLLM_MEANING_K": str(K), "UNLLM_TOP": str(K)}}


def found(question, env):
    os.environ.update(env)
    try:
        return [" ".join(p.split()) for p in run_unllm(CLI, question, passages=K)["passages"][:K]]
    finally:
        for k in env:
            del os.environ[k]


def main():
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    ranker = rerank.load("qwen3-0.6b")
    sets = {name: [dict(zip(("id", "question", "pattern"), l.split("\t")[:3]))
                   for l in open(f"{A}/{name}.tsv").read().splitlines()[1:]] for name in ("corpus50", "dev50")}
    hit = lambda pat, t: re.search(pat, t, re.I) is not None  # noqa: E731
    out = open(f"{A}/pool50.jsonl", "w")
    tot = {s: {a: {"found at all": 0, "top 10": 0, "top 1": 0} for a in ("words", "vectors", "pooled")} for s in sets}
    bar = Task("pool50", "Pooled search + reranker, 100 questions", total=sum(len(v) for v in sets.values()))
    for s, rows in sets.items():
        for q in rows:
            lists = {a: found(q["question"], env) for a, env in SEARCHES.items()}
            pool = list(dict.fromkeys(lists["vectors"] + lists["words"]))
            sc = dict(zip(pool, map(float, ranker.scores(q["question"], pool)))) if pool else {}
            rec = {"set": s, "id": q["id"], "question": q["question"], "pattern": q["pattern"]}
            for a, ps in (("words", lists["words"]), ("vectors", lists["vectors"]), ("pooled", pool)):
                rr = sorted(dict.fromkeys(ps), key=lambda p: -sc[p])
                r = {"found at all": any(hit(q["pattern"], p) for p in ps),
                     "top 10": any(hit(q["pattern"], p) for p in rr[:10]),
                     "top 1": bool(rr) and hit(q["pattern"], rr[0])}
                for k, v in r.items():
                    tot[s][a][k] += v
                rec[a] = dict(r, n=len(ps), top=[p[:200] for p in rr[:3]])
            out.write(json.dumps(rec) + "\n")
            out.flush()
            bar.step(detail=f"{s} {q['id']}")
    bar.finish()
    lines = [f"\n### Pooled search: words + vectors (top {K} each) + Qwen3-Reranker-0.6B, search only ({time.strftime('%Y-%m-%d %H:%M')})"]
    for s in sets:
        for a in ("words", "vectors", "pooled"):
            lines.append(f"- {s}, {a}: " + ", ".join(f"{k} {v}" for k, v in tot[s][a].items()) + " of 50")
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
