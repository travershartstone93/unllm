#!/usr/bin/env python3
"""Vector search vs word search, both with the Qwen3-Reranker-0.6B and unllm's plain quote, verified
dev 50, unllm's top 10 passages. The vector arm turns on the meaning layer over the full index
(vectors/wiki-full.unlpq); the word arm is the current defaults. Per-question rows go to
DATA/audit/rerank-vec50.jsonl."""
import json, os, re, sys, subprocess, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import rerank  # noqa: E402
from qa_score import run_unllm  # noqa: E402
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
FULL = os.path.expanduser("~/.local/share/unllm/vectors/wiki-full.unlpq")
DEFAULTS = "kinds,fact,super,roman,phrase,named,howmany,howto,of,rerank,leadfirst,attr,qa,defs,floor,union,floorwide,song,wide"
ARMS = {"words": {"UNLLM_LAYERS": DEFAULTS},
        "vectors": {"UNLLM_LAYERS": DEFAULTS + ",meaning", "UNLLM_MEANING": FULL}}
if os.environ.get("RERANKVEC_ONLY"):  # one arm, e.g. RERANKVEC_ONLY=vectors
    ARMS = {os.environ["RERANKVEC_ONLY"]: ARMS[os.environ["RERANKVEC_ONLY"]]}


def main():
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    ranker = rerank.load("qwen3-0.6b")
    rows = [dict(zip(("id", "question", "pattern", "type", "source"), l.split("\t")))
            for l in open(f"{A}/dev50.tsv").read().splitlines()[1:]]
    os.makedirs(f"{A}/tmp", exist_ok=True)

    def quote(q, ps):
        path = f"{A}/tmp/rerank-vec.txt"
        open(path, "w").write("".join(p + "\n" for p in ps))
        env = dict(os.environ, UNLLM_PASSAGES=path, UNLLM_LAYERS="")
        out = subprocess.run([CLI, q], capture_output=True, text=True, errors="replace", env=env, timeout=120).stdout
        return " ".join(out.split("\n\n")[0].split())

    hit = lambda pat, t: bool(t) and re.search(pat, t, re.I) is not None  # noqa: E731
    keys = ("answer in top 10", "top passage holds it, unllm order", "top passage holds it, reranked", "plain quote, reranked")
    c = {a: dict.fromkeys(keys, 0) for a in ARMS}
    out = open(f"{A}/rerank-vec50.jsonl", "w")
    bar = Task("rerankvec50", "Vector search + reranker, 50 questions", total=len(rows) * len(ARMS))
    for q in rows:
        rec = {"id": q["id"], "question": q["question"], "pattern": q["pattern"]}
        for arm, env in ARMS.items():
            os.environ.update(env)
            ps = [" ".join(p.split()) for p in run_unllm(CLI, q["question"], passages=10)["passages"][:10]]
            for k in env:
                del os.environ[k]
            sc = ranker.scores(q["question"], ps) if ps else []
            rr = [ps[k] for k in sorted(range(len(ps)), key=lambda k: -float(sc[k]))]
            ans = quote(q["question"], rr) if rr else ""
            r = {"answer in top 10": any(hit(q["pattern"], p) for p in ps),
                 "top passage holds it, unllm order": bool(ps) and hit(q["pattern"], ps[0]),
                 "top passage holds it, reranked": bool(rr) and hit(q["pattern"], rr[0]),
                 "plain quote, reranked": hit(q["pattern"], ans)}
            for k, v in r.items():
                c[arm][k] += v
            rec[arm] = dict(r, answer=ans, top=rr[:3])
            bar.step(detail=f"{q['id']} {arm}")
        out.write(json.dumps(rec) + "\n")
        out.flush()
    bar.finish()
    lines = [f"\n### {' vs '.join(ARMS)} + Qwen3-Reranker-0.6B, plain quote, verified dev 50 ({time.strftime('%Y-%m-%d %H:%M')})"]
    for k in keys:
        lines.append(f"- {k}: " + ", ".join(f"{a} {c[a][k]}" for a in ARMS) + " of 50")
    if len(ARMS) == 2:
        recs = [json.loads(l) for l in open(f"{A}/rerank-vec50.jsonl")]
        gain = sum(r["vectors"]["plain quote, reranked"] and not r["words"]["plain quote, reranked"] for r in recs)
        loss = sum(r["words"]["plain quote, reranked"] and not r["vectors"]["plain quote, reranked"] for r in recs)
        lines.append(f"- paired, plain quote: vectors right where words wrong {gain}, words right where vectors wrong {loss}")
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
