#!/usr/bin/env python3
"""Qwen3-Reranker-0.6B + qwen3:4b reader, verified dev 50, unllm's top 10 passages. Arms: 4B on the
reranked top 1 and top 3 (passages only), unllm's plain quote on the reranked order. All answers saved.
RERANK4B_VECTORS=1 turns on the meaning layer over the full index (vectors/wiki-full.unlpq)."""
import json, os, re, sys, subprocess, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import rerank  # noqa: E402
from qa_score import run_unllm  # noqa: E402
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
VEC = os.environ.get("RERANK4B_VECTORS") == "1"
DEFAULTS = "kinds,fact,super,roman,phrase,named,howmany,howto,of,rerank,leadfirst,attr,qa,defs,floor,union,floorwide,song,wide"
TAG = "rerank-4b-vec50" if VEC else "rerank-4b50"
PROMPT = """Answer the question using ONLY the numbered passages below; do not use your own knowledge.

Question: {q}

Passages:
{ps}

Give the shortest exact answer as it is stated in a passage, and that passage's number. If no passage states the answer, reply with answer "NONE" and passage 0.
Reply with only JSON: {{"answer": "...", "passage": N}}"""


def four_b(prompt):
    body = json.dumps({"model": "qwen3:4b", "messages": [{"role": "user", "content": prompt}], "stream": False, "format": "json",
                       "think": False, "options": {"temperature": 0, "num_ctx": 8192}}).encode()
    req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            o = json.loads(json.loads(r.read())["message"]["content"])
        return str(o.get("answer", "")).strip(), int(o.get("passage") or 0)
    except (ValueError, OSError, KeyError, TypeError):
        return "", 0


def main():
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    if VEC:
        os.environ.update(UNLLM_LAYERS=DEFAULTS + ",meaning",
                          UNLLM_MEANING=os.path.expanduser("~/.local/share/unllm/vectors/wiki-full.unlpq"))
    ranker = rerank.load("qwen3-0.6b")
    rows = [dict(zip(("id", "question", "pattern", "type", "source"), l.split("\t")))
            for l in open(f"{A}/dev50.tsv").read().splitlines()[1:]]
    os.makedirs(f"{A}/tmp", exist_ok=True)
    hit = lambda pat, t: bool(t) and t != "NONE" and re.search(pat, t, re.I) is not None  # noqa: E731
    out, bar = [], Task(TAG, ("Vectors + " if VEC else "") + "Reranker + 4B, 50 questions", total=len(rows))
    for q in rows:
        ps = [" ".join(p.split())[:900] for p in run_unllm(CLI, q["question"], passages=10)["passages"][:10]]
        sc = ranker.scores(q["question"], ps) if ps else []
        rr = [ps[k] for k in sorted(range(len(ps)), key=lambda k: -float(sc[k]))]
        fmt = lambda xs: "\n\n".join(f"[{i}] {p}" for i, p in enumerate(xs, 1))  # noqa: E731
        a1, n1 = four_b(PROMPT.format(q=q["question"], ps=fmt(rr[:1]))) if rr else ("NONE", 0)
        a3, n3 = four_b(PROMPT.format(q=q["question"], ps=fmt(rr[:3]))) if rr else ("NONE", 0)
        path = f"{A}/tmp/rerank-4b-rr.txt"
        open(path, "w").write("".join(p + "\n" for p in rr))
        pq = " ".join(subprocess.run([CLI, q["question"]], capture_output=True, text=True, errors="replace",
                                     env=dict(os.environ, UNLLM_PASSAGES=path, UNLLM_LAYERS="", UNLLM_MEANING=""), timeout=120).stdout.split("\n\n")[0].split()) if rr else ""
        out.append({"id": q["id"], "question": q["question"], "pattern": q["pattern"], "top_holds": bool(rr) and hit(q["pattern"], rr[0]),
                    "4b_top1": a1, "4b_top1_right": hit(q["pattern"], a1), "4b_top3": a3, "4b_top3_cited": n3,
                    "4b_top3_right": hit(q["pattern"], a3), "plain_quote": pq[:400], "plain_quote_right": hit(q["pattern"], pq)})
        bar.step(detail=q["id"])
    bar.finish()
    with open(f"{A}/{TAG}.jsonl", "w") as f:
        f.write("".join(json.dumps(r) + "\n" for r in out))
    s = lambda k: sum(r[k] for r in out)  # noqa: E731
    lines = [("\n### Vector search (full index) + Qwen3-Reranker-0.6B + qwen3:4b reader, verified dev 50 (2026-10-20)" if VEC
              else "\n### Qwen3-Reranker-0.6B + qwen3:4b reader, verified dev 50 (2026-10-19, night)"),
             f"- reranked top passage holds the answer: {s('top_holds')} of 50",
             f"- 4B on the reranked top 1: {s('4b_top1_right')} of 50; on the top 3: {s('4b_top3_right')} of 50",
             f"- unllm plain quote on the reranked order: {s('plain_quote_right')} of 50",
             f"- paired top 1: 4B right where plain quote wrong {sum(r['4b_top1_right'] and not r['plain_quote_right'] for r in out)}, "
             f"plain quote right where 4B wrong {sum(r['plain_quote_right'] and not r['4b_top1_right'] for r in out)}"]
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
