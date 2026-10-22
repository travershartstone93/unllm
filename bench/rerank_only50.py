#!/usr/bin/env python3
"""Reranker only (unllm's plain quote answers) vs reranker + DPR, verified dev 50, on unllm's
top 10 passages. Plain quote: the passage list goes in through UNLLM_PASSAGES with every layer
off, so the order is kept and no rule runs."""
import os, re, sys, subprocess
import torch
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import rerank  # noqa: E402
from qa_score import run_unllm  # noqa: E402
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
DPR = os.path.expanduser("~/Downloads/unllm-models/hf/dpr-reader-single-nq-base")
DEV = torch.device("cuda" if torch.cuda.is_available() else "cpu")


def main():
    from transformers import DPRReader, DPRReaderTokenizerFast
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    tok = DPRReaderTokenizerFast.from_pretrained(DPR)
    reader = DPRReader.from_pretrained(DPR).to(DEV).eval()
    ranker = rerank.load("qwen3-0.6b")
    rows = [dict(zip(("id", "question", "pattern", "type", "source"), l.split("\t")))
            for l in open(f"{A}/dev50.tsv").read().splitlines()[1:]]
    os.makedirs(f"{A}/tmp", exist_ok=True)

    def quote(q, ps, tag):
        path = f"{A}/tmp/rerank-only-{tag}.txt"
        open(path, "w").write("".join(p + "\n" for p in ps))
        env = dict(os.environ, UNLLM_PASSAGES=path, UNLLM_LAYERS="")
        out = subprocess.run([CLI, q], capture_output=True, text=True, errors="replace", env=env, timeout=120).stdout
        return " ".join(out.split("\n\n")[0].split())

    def dpr(q, ps):
        if not ps:
            return ""
        enc = tok(questions=q, titles=[p.split(": ", 1)[0] for p in ps], texts=[p.split(": ", 1)[-1] for p in ps],
                  return_tensors="pt", padding=True, truncation=True, max_length=350)
        with torch.no_grad():
            r = reader(**{k: v.to(DEV) for k, v in enc.items()})
        s = tok.decode_best_spans(enc, type(r)(start_logits=r.start_logits.cpu(), end_logits=r.end_logits.cpu(),
                                               relevance_logits=r.relevance_logits.cpu()), num_spans=1,
                                  num_spans_per_passage=1, max_answer_length=10)
        return s[0].text if s else ""

    hit = lambda pat, t: bool(t) and re.search(pat, t, re.I) is not None  # noqa: E731
    c = {k: 0 for k in ("top passage holds it, unllm order", "top passage holds it, reranked",
                        "plain quote, unllm order", "plain quote, reranked", "DPR, reranked top 1")}
    pq, dp = [], []
    bar = Task("rerankonly50", "Reranker only, 50 questions", total=len(rows))
    for q in rows:
        ps = [" ".join(p.split()) for p in run_unllm(CLI, q["question"], passages=10)["passages"][:10]]
        sc = ranker.scores(q["question"], ps) if ps else []
        rr = [ps[k] for k in sorted(range(len(ps)), key=lambda k: -float(sc[k]))]
        c["top passage holds it, unllm order"] += bool(ps) and hit(q["pattern"], ps[0])
        c["top passage holds it, reranked"] += bool(rr) and hit(q["pattern"], rr[0])
        a1 = hit(q["pattern"], quote(q["question"], ps, "orig")) if ps else False
        a2 = hit(q["pattern"], quote(q["question"], rr, "rr")) if rr else False
        a3 = hit(q["pattern"], dpr(q["question"], rr[:1]))
        c["plain quote, unllm order"] += a1
        c["plain quote, reranked"] += a2
        c["DPR, reranked top 1"] += a3
        pq.append(a2)
        dp.append(a3)
        bar.step(detail=q["id"])
    bar.finish()
    lines = ["\n### Reranker only vs reranker + DPR, verified dev 50, unllm's top 10 passages (2026-10-19, night)"]
    lines += [f"- {k}: {v} of 50" for k, v in c.items()]
    lines.append(f"- paired, reranked: DPR right where plain quote wrong {sum(d and not p for p, d in zip(pq, dp))}, "
                 f"plain quote right where DPR wrong {sum(p and not d for p, d in zip(pq, dp))}")
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
