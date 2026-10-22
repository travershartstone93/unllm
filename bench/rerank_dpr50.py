#!/usr/bin/env python3
"""Qwen3-Reranker-0.6B + DPR reader as unllm's answer step, verified dev 50, on unllm's top 10
passages (production defaults). Arms: DPR on the original order (10); reranker then DPR on
the top 1, top 3, all 10."""
import os, re, sys
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

    def dpr(q, ps):
        if not ps:
            return ""
        titles = [p.split(": ", 1)[0] for p in ps]
        texts = [p.split(": ", 1)[1] if ": " in p else p for p in ps]
        enc = tok(questions=q, titles=titles, texts=texts, return_tensors="pt", padding=True, truncation=True, max_length=350)
        with torch.no_grad():
            r = reader(**{k: v.to(DEV) for k, v in enc.items()})
        spans = tok.decode_best_spans(enc, type(r)(start_logits=r.start_logits.cpu(), end_logits=r.end_logits.cpu(),
                                                   relevance_logits=r.relevance_logits.cpu()),
                                      num_spans=1, num_spans_per_passage=1, max_answer_length=10)
        return spans[0].text if spans else ""

    arms = {"DPR alone, original order, 10": 0, "reranker + DPR, top 1": 0, "reranker + DPR, top 3": 0,
            "reranker + DPR, all 10": 0}
    no_passages = 0
    bar = Task("rerankdpr50", "Reranker + DPR, 50 questions", total=len(rows))
    for q in rows:
        ps = [" ".join(p.split()) for p in run_unllm(CLI, q["question"], passages=10)["passages"][:10]]
        if not ps:
            no_passages += 1
        sc = ranker.scores(q["question"], ps) if ps else []
        order = [ps[k] for k in sorted(range(len(ps)), key=lambda k: -float(sc[k]))]
        answers = {"DPR alone, original order, 10": dpr(q["question"], ps),
                   "reranker + DPR, top 1": dpr(q["question"], order[:1]),
                   "reranker + DPR, top 3": dpr(q["question"], order[:3]),
                   "reranker + DPR, all 10": dpr(q["question"], order)}
        for arm, ans in answers.items():
            arms[arm] += bool(ans) and re.search(q["pattern"], ans, re.I) is not None
        bar.step(detail=q["id"])
    bar.finish()
    lines = ["\n### Qwen3-Reranker-0.6B + DPR reader, verified dev 50, unllm's top 10 passages (2026-10-19, night)"]
    lines += [f"- {arm}: {n} of 50" for arm, n in arms.items()]
    lines.append(f"- questions with no passages (answered by unllm's calculator, not readable): {no_passages}")
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
