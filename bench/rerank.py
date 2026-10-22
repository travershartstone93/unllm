#!/usr/bin/env python3
"""Rerankers on the Round 1 article and passage jobs.

  rerank.py NAME [NAME ...]      names from MODELS below

Each reranker scores the question against every candidate and picks the
highest. Article candidates are scored on "Title: first passage unllm found
for that title" (the LLMs saw titles only); passage candidates on the same
400-character passages the LLMs saw. A reranker cannot pick "none of these"
or "none", so items whose reference answer is none always count as wrong.
Writes DATA/runs/rr-<name>/{article,passage}.jsonl in run.py's format, so
score.py ranks the rerankers next to the models. GPU, fp16; tray bar.
"""

import json
import os
import sys
import time

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from jobs import DATA  # noqa: E402
from taskprogress import Task  # noqa: E402

HF = os.path.expanduser("~/Downloads/unllm-models/hf")
MODELS = {
    "minilm-l6": ("crossenc", "ms-marco-MiniLM-L6-v2"),
    "bge-v2-m3": ("crossenc", "bge-reranker-v2-m3"),
    "bge-base": ("crossenc", "bge-reranker-base"),
    "qwen3-0.6b": ("qwen3", "Qwen3-Reranker-0.6B"),
    "mxbai-base-v2": ("mxbai", "mxbai-rerank-base-v2"),
}
DEV = torch.device("cuda" if torch.cuda.is_available() else "cpu")


class CrossEncoder:
    def __init__(self, path):
        from transformers import AutoModelForSequenceClassification, AutoTokenizer
        self.tok = AutoTokenizer.from_pretrained(path)
        self.model = AutoModelForSequenceClassification.from_pretrained(path, torch_dtype=torch.float16).to(DEV).eval()

    @torch.no_grad()
    def scores(self, q, docs):
        enc = self.tok([q] * len(docs), docs, truncation="only_second", max_length=512, padding=True,
                       return_tensors="pt").to(DEV)
        return self.model(**enc).logits[:, 0].float().tolist()


class Qwen3Reranker:
    """The model card's recipe: P("yes") at the last token after a fixed chat prompt."""
    PREFIX = ("<|im_start|>system\nJudge whether the Document meets the requirements based on the Query and the "
              "Instruct provided. Note that the answer can only be \"yes\" or \"no\".<|im_end|>\n<|im_start|>user\n")
    SUFFIX = "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
    TASK = "Given a question, retrieve passages that answer the question"

    def __init__(self, path):
        from transformers import AutoModelForCausalLM, AutoTokenizer
        self.tok = AutoTokenizer.from_pretrained(path, padding_side="left")
        self.model = AutoModelForCausalLM.from_pretrained(path, torch_dtype=torch.float16).to(DEV).eval()
        self.yes = self.tok.convert_tokens_to_ids("yes")
        self.no = self.tok.convert_tokens_to_ids("no")

    @torch.no_grad()
    def scores(self, q, docs, batch=8):
        out = []
        for k in range(0, len(docs), batch):  # small batches, and logits for the last token only: fits 8 GB
            texts = [self.PREFIX + f"<Instruct>: {self.TASK}\n<Query>: {q}\n<Document>: {d}" + self.SUFFIX
                     for d in docs[k:k + batch]]
            enc = self.tok(texts, truncation=True, max_length=1024, padding=True, return_tensors="pt").to(DEV)
            last = self.model(**enc, logits_to_keep=1).logits[:, -1, :]
            pair = torch.stack([last[:, self.no], last[:, self.yes]], dim=1).float()
            out += torch.log_softmax(pair, dim=1)[:, 1].exp().tolist()
        return out


class Mxbai:
    """mxbai-rerank v2's own recipe (mxbai_rerank 0.1, which fails on transformers 5):
    logit("1") minus logit("0") at the last token after a fixed chat prompt."""
    PREFIX = ("<|im_start|>system\nYou are Qwen, created by Alibaba Cloud. You are a helpful assistant.<|im_end|>\n"
              "<|im_start|>user\n")
    TASK = ("You are a search relevance expert who evaluates how well documents match search queries. For each "
            "query-document pair, carefully analyze the semantic relationship between them, then provide your binary "
            "relevance judgment (0 for not relevant, 1 for relevant).\nRelevance:")
    SUFFIX = "<|im_end|>\n<|im_start|>assistant\n"

    def __init__(self, path):
        from transformers import AutoModelForCausalLM, AutoTokenizer
        self.tok = AutoTokenizer.from_pretrained(path, padding_side="left")
        self.model = AutoModelForCausalLM.from_pretrained(path, torch_dtype=torch.float16).to(DEV).eval()
        self.yes = self.tok("1", add_special_tokens=False)["input_ids"][0]
        self.no = self.tok("0", add_special_tokens=False)["input_ids"][0]

    @torch.no_grad()
    def scores(self, q, docs, batch=8):
        out = []
        for k in range(0, len(docs), batch):
            texts = [self.PREFIX + f"query: {q}\ndocument: {d}\n" + self.TASK + self.SUFFIX for d in docs[k:k + batch]]
            enc = self.tok(texts, add_special_tokens=False, truncation=True, max_length=1024, padding=True,
                           return_tensors="pt").to(DEV)
            last = self.model(**enc, logits_to_keep=1).logits[:, -1, :]
            out += (last[:, self.yes] - last[:, self.no]).float().tolist()
        return out


def load(name):
    kind, folder = MODELS[name]
    path = os.path.join(HF, folder)
    return {"crossenc": CrossEncoder, "qwen3": Qwen3Reranker, "mxbai": Mxbai}[kind](path)


def passages_by_question():
    out = {}
    for name in ("dev", "nq"):
        for line in open(f"{DATA}/sets/pcache-{name}.jsonl"):
            c = json.loads(line)
            out[c["question"]] = c["reply"]["passages"]
    return out


def tasks():
    """(job, item id, question, candidate texts, answers in the same order)."""
    ps = passages_by_question()
    out = []
    for line in open(f"{DATA}/sets/items-article.jsonl"):
        it = json.loads(line)
        q = it["meta"]["question"]
        titles = [c for c in it["choices"] if c != "none of these"]
        first = {}
        for p in ps.get(q, []):
            t = p.split(": ", 1)[0]
            first.setdefault(t, " ".join(p.split())[:1200])
        out.append(("article", it["id"], q, [first.get(t, t) for t in titles], titles))
    for line in open(f"{DATA}/sets/items-passage.jsonl"):
        it = json.loads(line)
        q = it["meta"]["question"]
        cands = [" ".join(p.split())[:400] for p in ps.get(q, [])[:10]]
        out.append(("passage", it["id"], q, cands, [str(i) for i in range(1, len(cands) + 1)]))
    return out


def main():
    names = sys.argv[1:] or list(MODELS)
    work = tasks()
    bar = Task("rerank", "Rerankers: article and passage jobs", total=len(names) * len(work))
    for name in names:
        out_dir = f"{DATA}/runs/rr-{name}"
        os.makedirs(out_dir, exist_ok=True)
        model = load(name)
        files = {j: open(f"{out_dir}/{j}.jsonl", "w") for j in ("article", "passage")}
        for job, iid, q, cands, answers in work:
            t0 = time.time()
            s = model.scores(q, cands) if cands else []
            ans = answers[max(range(len(s)), key=s.__getitem__)] if s else None
            files[job].write(json.dumps({"id": iid, "answer": ans, "valid": ans is not None,
                                         "seconds": round(time.time() - t0, 3)}) + "\n")
            bar.step(detail=f"{name} {job}")
        for f in files.values():
            f.close()
        del model
        torch.cuda.empty_cache()
    bar.finish()


if __name__ == "__main__":
    main()
