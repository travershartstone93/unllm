#!/usr/bin/env python3
"""Export a faiss IVF-PQ index into the library's plain format (.unlpq) and lay out the
embedding model for the library, plus Python reference data for the C++ comparison.

  export_index.py pilot      the Simple-shard pilot index (DATA/vec/wikipedia-simple.ivfpq)
  export_index.py full       the full build (vectors/wiki-bge-small/index.ivfpq) to vectors/wiki-full.unlpq

File .unlpq, little-endian:
  magic "UNLPQ1\\0\\0"; int32 d, nlist, M, nbits, metric (0 = inner product), by_residual;
  int64 ntotal; int32 nshards; nshards x (int32 shard_no, 64-byte pack name, 64-byte file name);
  float32 centres[nlist][d]; float32 codebooks[M][2^nbits][d/M];
  zero padding to 8 bytes; then nlist x (int64 size, int64 ids[size], uint8 codes[size][M]).
"""

import json
import os
import struct
import sys

import faiss
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vec_pilot  # noqa: E402
from jobs import DATA  # noqa: E402

VECDIR = os.path.expanduser("~/.local/share/unllm/vectors")
MODELDIR = os.path.expanduser("~/.local/share/unllm/models/bge-small-en-v1.5")
ONNX = os.path.expanduser("~/Downloads/unllm-models/onnx/bge-small-en-v1.5/model-pos.onnx")
NPROBE = 64


def write_unlpq(index, out, shards, id_map):
    ivf = faiss.downcast_index(faiss.extract_index_ivf(index))
    quant = faiss.downcast_index(ivf.quantizer)
    pq = ivf.pq
    centres = quant.reconstruct_n(0, quant.ntotal).astype(np.float32)
    books = faiss.vector_to_array(pq.centroids).astype(np.float32).reshape(pq.M, pq.ksub, pq.dsub)
    inv = ivf.invlists
    with open(out + ".tmp", "wb") as f:
        f.write(b"UNLPQ1\0\0")
        f.write(struct.pack("<iiiiii", ivf.d, ivf.nlist, pq.M, pq.nbits, int(ivf.metric_type), int(ivf.by_residual)))
        f.write(struct.pack("<q", ivf.ntotal))
        f.write(struct.pack("<i", len(shards)))
        for no, pack, fname in shards:
            f.write(struct.pack("<i", no) + pack.encode().ljust(64, b"\0")[:64] + fname.encode().ljust(64, b"\0")[:64])
        f.write(b"\0" * (-f.tell() % 8))  # arrays start on 8 bytes
        f.write(centres.tobytes())
        f.write(books.tobytes())
        for l in range(ivf.nlist):
            n = inv.list_size(l)
            ids = faiss.rev_swig_ptr(inv.get_ids(l), n).astype(np.int64) if n else np.zeros(0, np.int64)
            codes = faiss.rev_swig_ptr(inv.get_codes(l), n * pq.code_size).astype(np.uint8) if n else np.zeros(0, np.uint8)
            f.write(struct.pack("<q", n))
            f.write(id_map(ids).astype("<i8").tobytes())
            f.write(codes.tobytes())
    os.replace(out + ".tmp", out)


def main():
    import onnxruntime as ort
    from transformers import AutoTokenizer
    os.makedirs(VECDIR, exist_ok=True)
    os.makedirs(MODELDIR, exist_ok=True)
    index = faiss.read_index(f"{vec_pilot.VEC}/{vec_pilot.PACK}.ivfpq")
    index.nprobe = NPROBE
    shards = [(100, "wikipedia-simple", "shard-000.sqlite")]
    # The pilot index was filled with plain add(), so it stores row numbers; the pilot's pid
    # array maps them to passage ids (the full build stores shard << 32 | pid directly).
    pids = np.load(f"{vec_pilot.VEC}/{vec_pilot.PACK}.pids.npy").astype(np.int64)
    id_map = lambda ids: pids[ids] + (100 << 32)  # noqa: E731
    write_unlpq(index, f"{VECDIR}/wikipedia-simple.unlpq", shards, id_map)
    tok = AutoTokenizer.from_pretrained(vec_pilot.MODEL)
    vocab = json.load(open(f"{vec_pilot.MODEL}/tokenizer.json"))["model"]["vocab"]
    with open(f"{MODELDIR}/vocab.txt", "w") as f:
        for piece, _ in sorted(vocab.items(), key=lambda kv: kv[1]):
            f.write(piece + "\n")
    if not os.path.exists(f"{MODELDIR}/model.onnx"):
        os.link(ONNX, f"{MODELDIR}/model.onnx")
    json.dump({"model": "BAAI/bge-small-en-v1.5", "dim": index.d, "query_prefix": vec_pilot.QUERY_PREFIX, "max_len": 64,
               "inputs": ["input_ids", "attention_mask", "token_type_ids", "position_ids"], "pooling": "cls", "normalize": True},
              open(f"{MODELDIR}/embed.json", "w"), indent=1)
    # Python references: question vectors through the same ONNX file, and faiss top 20 at nprobe 64.
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    sess = ort.InferenceSession(ONNX, providers=["CPUExecutionProvider"])
    vecs = []
    for it in items:
        e = tok([vec_pilot.QUERY_PREFIX + it["question"]], truncation=True, max_length=64, return_tensors="np")
        out = sess.run(None, {"input_ids": e["input_ids"], "attention_mask": e["attention_mask"], "token_type_ids": e["token_type_ids"],
                              "position_ids": np.arange(e["input_ids"].shape[1], dtype=np.int64)[None]})[0][:, 0]
        vecs.append(out[0] / np.linalg.norm(out[0]))
    q = np.stack(vecs).astype(np.float32)
    os.makedirs(f"{DATA}/audit", exist_ok=True)
    np.save(f"{DATA}/audit/ref-vectors.npy", q)
    _, ids = index.search(q, 20)
    json.dump({it["id"]: [int(id_map(np.array([x]))[0]) for x in row if x >= 0] for it, row in zip(items, ids)},
              open(f"{DATA}/audit/ref-top20.json", "w"))
    print(f"exported: d {index.d} nlist {index.nlist} M {index.pq.M} ntotal {index.ntotal:,} by_residual {index.by_residual} "
          f"metric {index.metric_type}; file {os.path.getsize(f'{VECDIR}/wikipedia-simple.unlpq') / 1e6:.0f} MB; "
          f"vocab {sum(1 for _ in open(f'{MODELDIR}/vocab.txt'))} tokens; references for {len(items)} questions")


def full():
    """The full build already stores shard_no << 32 | pid, so its ids are written as they are."""
    index = faiss.read_index(f"{VECDIR}/wiki-bge-small/index.ivfpq", faiss.IO_FLAG_MMAP | faiss.IO_FLAG_READ_ONLY)
    prog = json.load(open(f"{VECDIR}/wiki-bge-small/progress.json"))
    names = {n: p for p, n in prog.get("packs", {}).items()}
    names[100] = "wikipedia-simple"
    shards = [(no, names.get(no, "wikipedia-en"), f"shard-{0 if no >= 100 else no:03d}.sqlite")
              for no in sorted(int(k) for k in prog["done"])]
    write_unlpq(index, f"{VECDIR}/wiki-full.unlpq", shards, lambda ids: ids)
    print(f"exported {index.ntotal:,} passages from {len(shards)} shards; "
          f"file {os.path.getsize(f'{VECDIR}/wiki-full.unlpq') / 1e9:.2f} GB")


if __name__ == "__main__":
    full() if sys.argv[1:] == ["full"] else main()
