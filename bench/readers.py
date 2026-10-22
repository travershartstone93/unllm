#!/usr/bin/env python3
"""Extractive readers side by side on unllm's own passages.

  readers.py NAME [NAME ...]     names from MODELS below

For every clean dev and NQ300 question, the passages unllm found (cached in
DATA/sets/pcache-*.jsonl; one passage per article, up to 20, as "Title: text",
like the C++ reader) go to each reader. Scored against the answer keys, right
out of all, three ways:
  top span   the reader's best span (by score minus no-answer score) alone
  gated      that span when it passes unllm's vote gate (the same span from 2+
             articles, or probability >= 0.9), otherwise unllm's own answer
  unllm      unllm's own answer, for reference
Only the span is judged (the C++ reply adds a support sentence, which can only
make it look better). GPU, fp16; saves DATA/readers/<name>-<set>.jsonl; tray bar.
"""

import json
import os
import re
import sys

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from evaluate import judge, nq_items, tsv_items  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right, pattern_right  # noqa: E402
from taskprogress import Task  # noqa: E402

HF = os.path.expanduser("~/Downloads/unllm-models/hf")
REPO = os.path.expanduser("~/repo-work/unllm")
MODELS = {"minilm": "minilm-uncased-squad2", "roberta": "roberta-base-squad2", "deberta": "deberta-v3-base-squad2"}
DEV = torch.device("cuda" if torch.cuda.is_available() else "cpu")
STOP = set("the a an of in on at to and or for is was by with from as its it his her their".split())


def sets():
    cache = {}
    for name in ("dev", "nq"):
        for line in open(f"{DATA}/sets/pcache-{name}.jsonl"):
            c = json.loads(line)
            cache[c["question"]] = c["reply"]
    dev = [it for it in tsv_items(REPO + "/tests/clean/dev.tsv") if not it.get("unknowable")]
    return {"dev": (dev, cache), "nq": (nq_items(300, 11, None), cache)}


def one_per_article(passages):
    seen, out = set(), []
    for p in passages[:20]:
        t = p.split(": ", 1)[0]
        if t not in seen:
            seen.add(t)
            out.append(p)
    return out


class Reader:
    def __init__(self, folder):
        from transformers import AutoModelForQuestionAnswering, AutoTokenizer
        path = os.path.join(HF, folder)
        self.tok = AutoTokenizer.from_pretrained(path)
        self.model = AutoModelForQuestionAnswering.from_pretrained(path, torch_dtype=torch.float16).to(DEV).eval()

    @torch.no_grad()
    def read(self, q, passages):
        enc = self.tok([q] * len(passages), passages, truncation="only_second", max_length=384, padding=True,
                       return_offsets_mapping=True, return_tensors="pt")
        offsets = enc.pop("offset_mapping")
        out = self.model(**{k: v.to(DEV) for k, v in enc.items()})
        st, en = out.start_logits.float().cpu(), out.end_logits.float().cpu()
        spans = []
        for i, p in enumerate(passages):
            ctx = [j for j, s in enumerate(enc.sequence_ids(i)) if s == 1]
            if not ctx:
                continue
            lo, hi = ctx[0], ctx[-1]
            s, e = st[i], en[i]
            null = float(s[0] + e[0])
            si = torch.topk(s[lo:hi + 1], min(20, hi - lo + 1)).indices + lo
            ei = torch.topk(e[lo:hi + 1], min(20, hi - lo + 1)).indices + lo
            best = (-1e9, 0, 0)
            for a in si.tolist():
                for b in ei.tolist():
                    if a <= b < a + 30 and float(s[a] + e[b]) > best[0]:
                        best = (float(s[a] + e[b]), a, b)
            text = p[int(offsets[i][best[1]][0]):int(offsets[i][best[2]][1])].strip()
            ps = torch.softmax(s[lo:hi + 1], 0).max().item()
            pe = torch.softmax(e[lo:hi + 1], 0).max().item()
            spans.append({"passage": i, "text": text, "margin": best[0] - null, "prob": ps * pe})
        return spans


def keys(text):
    return {w for w in re.findall(r"\w+", text.lower()) if w not in STOP}


def gate(spans):
    """unllm's rule: best by margin; accept with 2+ agreeing articles or probability >= 0.9."""
    good = [s for s in spans if s["margin"] > 0 and s["text"]]
    if not good:
        return None, 0
    best = max(good, key=lambda s: s["margin"])
    k = keys(best["text"])
    votes = sum(1 for s in good if k and (k <= keys(s["text"]) or keys(s["text"]) <= k) and keys(s["text"]))
    return (best if votes >= 2 or best["prob"] >= 0.9 else None), votes


def right(it, text):
    return nq_right(text, it["aliases"]) if "aliases" in it else pattern_right(text, it["pattern"])


def main():
    names = sys.argv[1:] or list(MODELS)
    data = sets()
    os.makedirs(f"{DATA}/readers", exist_ok=True)
    bar = Task("readers", "Readers: dev and NQ300", total=len(names) * sum(len(v[0]) for v in data.values()))
    lines = []
    for name in names:
        reader = Reader(MODELS[name])
        for set_name, (items, cache) in data.items():
            n = top = gated = alone = 0
            with open(f"{DATA}/readers/{name}-{set_name}.jsonl", "w") as f:
                for it in items:
                    reply = cache.get(it["question"])
                    if reply is None:
                        bar.step()
                        continue
                    ps = one_per_article(reply["passages"])
                    spans = reader.read(it["question"], ps) if ps else []
                    good = [s for s in spans if s["text"]]
                    best = max(good, key=lambda s: s["margin"]) if good else None
                    passed, votes = gate(spans)
                    u = judge(it, reply)[1]
                    r_top = bool(best and right(it, best["text"]))
                    r_gated = right(it, passed["text"]) if passed else u
                    n += 1
                    top += r_top
                    gated += r_gated
                    alone += u
                    f.write(json.dumps({"id": it["id"], "best": best, "passed": passed is not None, "votes": votes,
                                        "top_right": r_top, "gated_right": bool(r_gated), "unllm_right": bool(u)}) + "\n")
                    bar.step(detail=f"{name} {set_name}")
            lines.append(f"{name:8} {set_name:4} n={n}  top span {top}  gated {gated}  unllm alone {alone}")
            print(lines[-1], flush=True)
        del reader
        torch.cuda.empty_cache()
    bar.finish()
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/readers.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
