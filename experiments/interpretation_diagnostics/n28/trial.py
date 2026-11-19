"""Fit field-specific 1-NN heads on reviewed public samples with a fixed English encoder."""
from pathlib import Path
import json
import re
import sys
import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer
from text_to_num import alpha2digit

root = Path.cwd()
def records(n, file):
    directory = Path((root / f".state/n{n}.path").read_text().strip())
    return [json.loads(line) for line in (directory / file).read_text().splitlines()]

folder = Path.home() / ".cache/chroma/onnx_models/all-MiniLM-L6-v2/onnx"
tokenizer = Tokenizer.from_file(str(folder / "tokenizer.json"))
tokenizer.enable_padding()
opts = ort.SessionOptions()
opts.intra_op_num_threads = 2
encoder = ort.InferenceSession(str(folder / "model.onnx"), opts, providers=["CPUExecutionProvider"])
def embed(texts):
    tokens = tokenizer.encode_batch(texts)
    args = {key: np.array([getattr(t, attr) for t in tokens], dtype="int64")
            for key, attr in (("input_ids","ids"), ("attention_mask","attention_mask"), ("token_type_ids","type_ids"))}
    mask = args["attention_mask"][:,:,None]
    pooled = (encoder.run(None,args)[0] * mask).sum(axis=1) / mask.sum(axis=1)
    return pooled / np.linalg.norm(pooled,axis=1,keepdims=True)

training = {}
for r in records(26,"direct.jsonl"):
    training[r["request"]] = (r["requirements"], f"CoNaLa {r['row']}")
seed_size = len(training)
bound = {r["row"]:r for r in records(27,"contextual.jsonl")}
for r in records(27,"reviewed-fields.jsonl"):
    s = r["source"]
    b = bound.get(s["row"])
    question = s["question"] if b is None else b["binding"] + " " + s["question"]
    training[question] = (r["reviewed_fields"] if b is None else b["requirements"], f"WikiSQL {s['row']}")
keys = list(training)
vectors = embed(keys)
test = records(26,"holdout.jsonl")
similarity = embed([r["request"] for r in test]) @ vectors.T
for size in [seed_size, len(training)]:
    for case, scores in zip(test, similarity):
        order = np.argsort(-scores[:size],kind="stable")
        prediction, sources = {}, {}
        for field in ["status","aggregate","collection","comparison","required_structure"]:
            for i in order:
                label, source = training[keys[i]]
                if field in label:
                    prediction[field],sources[field] = label[field],source
                    break
        # Shared numeric grounding baseline: first explicit integer, otherwise the supplied scalar.
        words = alpha2digit(case["request"], "en")
        number = re.search(r"(?<!\w)[+-]?\d+(?!\w)", words)
        prediction["compare_to"] = {"literal":int(number[0])} if number else {"input":"cutoff"}
        print(json.dumps(dict(training_rows=size, id=case["id"], request=case["request"],
                              output=prediction, expected=case["expected"], sources=sources,
                              exact=prediction==case["expected"])))
