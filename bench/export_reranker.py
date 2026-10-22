#!/usr/bin/env python3
"""Lay out the bge-reranker-v2-m3 files for the library and write the C++ check's reference.
Model: the half-precision ONNX export (onnx/bge-reranker-v2-m3/model-fp16.onnx + .data) and the
SentencePiece model, into ~/.local/share/unllm/models/reranker/ with rerank.json. Reference:
DATA/audit/rerank-ref.jsonl, 24 (question, passage) pairs from the saved pools with the token ids
the Hugging Face tokenizer produces and the score the ONNX model gives on the GPU."""
import json, os, shutil
import numpy as np, torch  # noqa: F401  (torch first: it loads the CUDA libraries the runtime needs)
import onnxruntime as ort
from transformers import AutoTokenizer

HF = os.path.expanduser("~/Downloads/unllm-models/hf/bge-reranker-v2-m3")
ONNX = os.path.expanduser("~/Downloads/unllm-models/onnx/bge-reranker-v2-m3")
OUT = os.path.expanduser("~/.local/share/unllm/models/reranker")
A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
MAX_LEN = 512


def main():
    for f in ("model-fp16.onnx", "model-fp16.onnx.data"):
        if not os.path.exists(f"{OUT}/{f}") or os.path.getsize(f"{OUT}/{f}") != os.path.getsize(f"{ONNX}/{f}"):
            shutil.copy(f"{ONNX}/{f}", f"{OUT}/{f}")
    shutil.copy(f"{HF}/sentencepiece.bpe.model", f"{OUT}/sentencepiece.bpe.model")
    json.dump({"model": "BAAI/bge-reranker-v2-m3", "file": "model-fp16.onnx", "tokenizer": "sentencepiece.bpe.model",
               "max_len": MAX_LEN, "batch": 8, "cls": 0, "pad": 1, "sep": 2, "unk": 3, "offset": 1,
               "pair": "<s> question </s></s> passage </s>", "document": "title: text"},
              open(f"{OUT}/rerank.json", "w"), indent=1)
    tok = AutoTokenizer.from_pretrained(HF)
    sess = ort.InferenceSession(f"{OUT}/model-fp16.onnx", providers=["CUDAExecutionProvider", "CPUExecutionProvider"])
    rows = [json.loads(l) for l in open(f"{A}/pool-top10-step3.jsonl")]
    pairs = []
    for r in rows[:12]:
        pairs += [(r["question"], p) for p in r["pool"][:2]]
    with open(f"{A}/rerank-ref.jsonl", "w") as f:
        for q, p in pairs:
            e = tok([q], [p], truncation="only_second", max_length=MAX_LEN, return_tensors="np")
            score = float(sess.run(None, {"input_ids": e["input_ids"], "attention_mask": e["attention_mask"]})[0][0, 0])
            f.write(json.dumps({"question": q, "passage": p, "ids": e["input_ids"][0].tolist(), "score": score}) + "\n")
    print(f"installed to {OUT}: {sorted(os.listdir(OUT))}; reference pairs {len(pairs)} on {sess.get_providers()[0]}")


if __name__ == "__main__":
    main()
