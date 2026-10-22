#!/usr/bin/env python3
"""The two checks that decide whether a full Wikipedia vector build may start.

  vec_checks.py speed      how fast can this GPU embed? Pilot setting (batches
                           in file order, 256-token cap) against length-sorted
                           batches with a 192-token cap, on 40,000 passages of an
                           English Wikipedia shard. Reports passages/s and the
                           implied hours for all 62.7M passages.
  vec_checks.py compress   squeeze the Simple shard's vectors with faiss IVF-PQ
                           (48 bytes per vector) and rerun the 400 pilot
                           questions: found-in-top-20 against the exact search,
                           and seconds per query on the CPU.

Results append to the pilot report.
"""

import os
import sqlite3
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from qa_score import nq_right  # noqa: E402

REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md")
EN_SHARD = os.path.expanduser("~/.local/share/unllm/packs/wikipedia-en/shard-013.sqlite")
ALL_EN = 62_749_913


def note(text):
    print(text, flush=True)
    with open(REPORT, "a") as f:
        f.write(text + "\n")


def speed():
    import torch
    con = sqlite3.connect(f"file:{EN_SHARD}?mode=ro", uri=True)
    rows = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id=p.doc_id ORDER BY p.pid LIMIT 40000").fetchall()
    texts = [f"{t}: {' '.join(x.split())[:1500]}" for t, x in rows]
    encode = vec_pilot.encoder()
    encode(texts[:256], 256)  # warm up
    torch.cuda.synchronize()
    t0 = time.time()
    for k in range(0, 20000, 256):
        encode(texts[k:k + 256], 256)
    torch.cuda.synchronize()
    a = 20000 / (time.time() - t0)
    from transformers import AutoTokenizer
    tok = AutoTokenizer.from_pretrained(vec_pilot.MODEL)
    t0 = time.time()
    lens = [len(x) for x in tok(texts, truncation=True, max_length=192, add_special_tokens=True)["input_ids"]]
    order = sorted(range(len(texts)), key=lambda i: -lens[i])
    budget = 256 * 192  # tokens per batch: short passages get bigger batches
    batches, k = [], 0
    while k < len(order):
        n = max(64, min(1024, budget // max(lens[order[k]], 8)))
        batches.append([texts[i] for i in order[k:k + n]])
        k += n
    for b in batches:
        encode(b, 192)
    torch.cuda.synchronize()
    b_rate = len(texts) / (time.time() - t0)
    note("\n### Speed check\n"
         f"- pilot setting (file order, 256-token cap): {a:.0f} passages/s -> all of English Wikipedia in {ALL_EN / a / 3600:.0f} h\n"
         f"- length-sorted, 192-token cap (includes tokenising): {b_rate:.0f} passages/s -> {ALL_EN / b_rate / 3600:.0f} h\n"
         f"- verdict: {'PASS' if b_rate >= 500 else 'FAIL'} (mark: 500/s)")


def compress():
    import faiss
    import torch
    vecs = np.load(f"{vec_pilot.VEC}/{vec_pilot.PACK}.npy", mmap_mode="r")
    pids = np.load(f"{vec_pilot.VEC}/{vec_pilot.PACK}.pids.npy")
    x = np.ascontiguousarray(vecs[:], dtype=np.float32)
    d = x.shape[1]
    t0 = time.time()
    quant = faiss.IndexFlatIP(d)
    index = faiss.IndexIVFPQ(quant, d, 2048, 48, 8, faiss.METRIC_INNER_PRODUCT)
    rng = np.random.default_rng(0)
    index.train(x[rng.choice(len(x), 150_000, replace=False)])
    index.add(x)
    build = time.time() - t0
    faiss.write_index(index, f"{vec_pilot.VEC}/{vec_pilot.PACK}.ivfpq")
    size = os.path.getsize(f"{vec_pilot.VEC}/{vec_pilot.PACK}.ivfpq")
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    encode = vec_pilot.encoder()
    q = torch.cat([encode([vec_pilot.QUERY_PREFIX + it["question"] for it in items[k:k + 64]], 64)
                   for k in range(0, len(items), 64)]).float().cpu().numpy()
    del encode
    torch.cuda.empty_cache()
    con = vec_pilot.shard()
    lines = ["\n### Compression check",
             f"- IVF-PQ 2048 lists x 48 bytes: index file {size / 1e6:.0f} MB for {len(x):,} vectors = {size / len(x):.0f} bytes each; built in {build / 60:.1f} min"]
    for nprobe in (16, 32, 64):
        index.nprobe = nprobe
        t0 = time.time()
        _, idx = index.search(q, 20)
        per = (time.time() - t0) / len(items)
        found = 0
        for it, row in zip(items, idx):
            ps = []
            for j in row:
                if j < 0:
                    continue
                title, text = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id=p.doc_id "
                                          "WHERE p.pid=?", (int(pids[j]),)).fetchone()
                ps.append(f"{title}: {text}")
            found += any(nq_right(p, it["aliases"]) for p in ps)
        lines.append(f"- nprobe {nprobe}: found in top 20 {found} of {len(items)} ({100 * found / len(items):.0f}%), "
                     f"{per * 1000:.0f} ms per query on the CPU {'PASS' if found >= 280 else 'FAIL'} (mark 280; exact search 310)")
    note("\n".join(lines))


if __name__ == "__main__":
    {"speed": speed, "compress": compress}[sys.argv[1]]()
