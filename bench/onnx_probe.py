#!/usr/bin/env python3
"""Route B of the spine audit: bge-small-en-v1.5 through ONNX Runtime (which has a C++ API the
library could link), so the library can embed questions itself. Exports the model, checks the
vectors against PyTorch on 50 pilot questions, times single questions on the CPU, then the
same for an 8-bit quantised copy. Marks: cosine >= 0.999 full / >= 0.99 8-bit; <= 50 ms a
question at one thread. Files under ~/Downloads/unllm-models/onnx/bge-small-en-v1.5/.
"""

import os
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vec_pilot  # noqa: E402

OUT = os.path.expanduser("~/Downloads/unllm-models/onnx/bge-small-en-v1.5")
REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md")


def main():
    from transformers import AutoModel, AutoTokenizer
    import onnxruntime as ort
    os.makedirs(OUT, exist_ok=True)
    tok = AutoTokenizer.from_pretrained(vec_pilot.MODEL)
    model = AutoModel.from_pretrained(vec_pilot.MODEL, torch_dtype=torch.float32, attn_implementation="eager").eval()
    enc = tok(["hello world"], return_tensors="pt")
    path = f"{OUT}/model-pos.onnx"

    class Wrap(torch.nn.Module):  # inputs by name: the exporter's positional order does not match the model's
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, input_ids, attention_mask, token_type_ids, position_ids):
            return self.m(input_ids=input_ids, attention_mask=attention_mask, token_type_ids=token_type_ids,
                          position_ids=position_ids, return_dict=True).last_hidden_state

    if not os.path.exists(path):
        pos = torch.arange(enc["input_ids"].shape[1])[None]
        args = (enc["input_ids"], enc["attention_mask"], enc["token_type_ids"], pos)
        kw = dict(input_names=["input_ids", "attention_mask", "token_type_ids", "position_ids"], output_names=["last_hidden_state"],
                  dynamic_axes={k: {0: "batch", 1: "seq"} for k in ("input_ids", "attention_mask", "token_type_ids", "position_ids", "last_hidden_state")},
                  opset_version=17)
        try:
            torch.onnx.export(Wrap(model), args, path, dynamo=False, **kw)
        except Exception as e:  # noqa: BLE001
            print("legacy exporter failed:", str(e)[:120], "- trying the new one")
            torch.onnx.export(Wrap(model), args, path, dynamo=True, **kw)
    tok.save_pretrained(OUT)
    qs = [vec_pilot.QUERY_PREFIX + it["question"] for it in vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")[:50]]
    with torch.no_grad():
        ref = np.concatenate([torch.nn.functional.normalize(
            model(**tok([q], truncation=True, max_length=64, return_tensors="pt"), return_dict=True).last_hidden_state[:, 0],
            dim=1).numpy() for q in qs])

    def feed(e):
        return {"input_ids": e["input_ids"], "attention_mask": e["attention_mask"], "token_type_ids": e["token_type_ids"],
                "position_ids": np.arange(e["input_ids"].shape[1], dtype=np.int64)[None]}

    def check(file, label):
        so = ort.SessionOptions()
        res = {}
        for threads in (1, 4):
            so.intra_op_num_threads = threads
            sess = ort.InferenceSession(file, so, providers=["CPUExecutionProvider"])
            vecs = []
            for q in qs:
                e = tok([q], truncation=True, max_length=64, return_tensors="np")
                out = sess.run(None, feed(e))[0]
                v = out[:, 0]
                vecs.append(v / np.linalg.norm(v, axis=1, keepdims=True))
            t = time.time()
            for q in qs[:20]:
                e = tok([q], truncation=True, max_length=64, return_tensors="np")
                sess.run(None, feed(e))
            res[threads] = (time.time() - t) / 20 * 1000
            cos = (np.concatenate(vecs) * ref).sum(1)
        size = os.path.getsize(file) / 1e6
        return f"{label}: {size:.0f} MB; cosine to PyTorch min {cos.min():.4f} mean {cos.mean():.4f}; per question {res[1]:.1f} ms (1 thread), {res[4]:.1f} ms (4 threads)", cos.min(), res[1]

    lines = []
    sess0 = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    with torch.no_grad():
        e0 = tok(["hello world"], return_tensors="pt")
        r0 = torch.nn.functional.normalize(model(**e0, return_dict=True).last_hidden_state[:, 0], dim=1).numpy()[0]
    o0 = sess0.run(None, feed(tok(["hello world"], return_tensors="np")))[0][:, 0][0]
    lines.append(f"sanity on the trace-length input: cosine {float(np.dot(r0, o0 / np.linalg.norm(o0))):.4f}")
    full, cos_f, ms_f = check(path, "full precision")
    lines.append(full)
    from onnxruntime.quantization import quantize_dynamic, QuantType
    q8 = f"{OUT}/model-pos-int8.onnx"
    if not os.path.exists(q8):
        quantize_dynamic(path, q8, weight_type=QuantType.QInt8)
    small, cos_q, ms_q = check(q8, "8-bit weights")
    lines.append(small)
    verdict = f"{'PASS' if cos_f >= 0.999 and ms_f <= 50 else 'FAIL'} full (marks cosine 0.999, 50 ms); {'PASS' if cos_q >= 0.99 and ms_q <= 50 else 'FAIL'} 8-bit (marks 0.99, 50 ms)"
    lines.append(verdict)
    print("\n".join(lines))
    with open(REPORT, "a") as f:
        f.write("\n### Route B, bge-small through ONNX Runtime (2026-10-19)\n" + "".join(f"- {l}\n" for l in lines))


if __name__ == "__main__":
    main()
