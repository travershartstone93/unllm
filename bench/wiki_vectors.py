#!/usr/bin/env python3
"""Vector index over the Wikipedia packs: every passage of wikipedia-en (15
shards) and wikipedia-simple embedded with bge-small-en-v1.5 and stored in one
compressed faiss index (IVF-PQ, about 56 bytes a passage), so meaning-based
search needs about 4 GB of disk and no raw vectors.

  wiki_vectors.py build            resumable; checkpoints every 1,000,000
                                   passages; CPU and GPU work at the same time
                                   (a helper thread tokenises the next chunk,
                                   the GPU picks each vector's list); run it
                                   under bench/heatwatch.sh, which freezes it
                                   when hot or on battery; BUILD_THREADS (4)
                                   caps the CPU side; tray bar
  wiki_vectors.py search "question" [k]      try it
  wiki_vectors.py test             the 400 pilot questions and the 300 fresh
                                   NQ questions: found in top 20 / top 5

Index ids encode the source: id = shard_no * 2^32 + pid, with shard_no 0..14
for wikipedia-en, 100 for wikipedia-simple and 101 up for every other pack
(Stack Exchange, dictionaries, poetry), numbered once and kept in
progress.json under "packs". Files live in
~/.local/share/unllm/vectors/wiki-bge-small/ (index.ivfpq, progress.json,
manifest.json). The trained index comes from the first 700,000 passages of
the smallest shard; wikipedia-simple reuses the pilot's vectors.
"""

import glob
import json
import os
import sqlite3
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from recall import cool_down  # noqa: E402
from taskprogress import Task  # noqa: E402

PACKS = os.path.expanduser("~/.local/share/unllm/packs")
OUT = os.path.expanduser("~/.local/share/unllm/vectors/wiki-bge-small")
INDEX = f"{OUT}/index.ivfpq"
PROGRESS = f"{OUT}/progress.json"
DIM = 384
NLIST, M, NBITS = 16384, 48, 8
TRAIN_N = 700_000
CHUNK = 20_000           # passages sorted by length at a time
CKPT = 1_000_000         # save the index this often within a shard (about 18 minutes)
BUDGET = 256 * 192       # tokens per GPU batch
MAX_LEN = 192
SIMPLE_ID = 100


def shards(prog):
    """(shard_no, path) for every shard to index: wikipedia-en first (smallest first), then simple,
    then every other pack. A pack's number is given once and kept in prog["packs"]."""
    en = sorted(glob.glob(f"{PACKS}/wikipedia-en/shard-*.sqlite"), key=os.path.getsize)
    out = [(int(os.path.basename(p)[6:9]), p) for p in en]
    out.append((SIMPLE_ID, f"{PACKS}/wikipedia-simple/shard-000.sqlite"))
    nos = prog.setdefault("packs", {})
    for path in sorted(glob.glob(f"{PACKS}/*/shard-000.sqlite")):
        pack = os.path.basename(os.path.dirname(path))
        if pack.startswith("wikipedia-"):
            continue
        if pack not in nos:
            nos[pack] = max([SIMPLE_ID, *nos.values()]) + 1
        out.append((nos[pack], path))
    return out


def shard_path(no):
    """The sqlite file behind a shard number."""
    if no == SIMPLE_ID:
        return f"{PACKS}/wikipedia-simple/shard-000.sqlite"
    if no < SIMPLE_ID:
        return f"{PACKS}/wikipedia-en/shard-{no:03d}.sqlite"
    packs = json.load(open(PROGRESS)).get("packs", {})
    return f"{PACKS}/{next(p for p, n in packs.items() if n == no)}/shard-000.sqlite"


def gpu_temp():
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=temperature.gpu", "--format=csv,noheader"],
                             capture_output=True, text=True, timeout=10).stdout.strip()
        return float(out) if out else 0.0
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return 0.0


def cool():
    cool_down()
    while gpu_temp() > 80:
        print(f"  gpu at {gpu_temp()} C, pausing 30 s", flush=True)
        time.sleep(30)


def passages(path, after_pid=-1):
    """(pid, "title: text") for every passage of a shard, in pid order, after after_pid.
    900 characters is more than the 192-token cap can take, so the tokeniser gets no more."""
    con = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    for pid, title, text in con.execute("SELECT p.pid, d.title, p.text FROM passages p JOIN docs d ON d.doc_id=p.doc_id "
                                        "WHERE p.pid > ? ORDER BY p.pid", (after_pid,)):
        yield pid, f"{title}: {' '.join(text.split())[:900]}"


def encoder_ids():
    """The embedding model fed pre-tokenised batches (tokenise once, not twice). Returns (tokeniser, encode, model)."""
    import torch
    from transformers import AutoModel, AutoTokenizer
    tok = AutoTokenizer.from_pretrained(vec_pilot.MODEL)
    model = AutoModel.from_pretrained(vec_pilot.MODEL, dtype=torch.float16).cuda().eval()
    pad = tok.pad_token_id

    @torch.no_grad()
    def encode(ids_list):
        n = max(len(x) for x in ids_list)
        ids = torch.full((len(ids_list), n), pad, dtype=torch.long)
        mask = torch.zeros((len(ids_list), n), dtype=torch.long)
        for i, x in enumerate(ids_list):
            ids[i, :len(x)] = torch.tensor(x)
            mask[i, :len(x)] = 1
        out = model(input_ids=ids.cuda(), attention_mask=mask.cuda()).last_hidden_state[:, 0]  # CLS pooling
        return torch.nn.functional.normalize(out.float(), dim=1)
    return tok, encode, model


def batches(chunk, tok):
    """Tokenises a chunk once; yields (pids, token id lists) in length-sorted batches under a
    token budget, so short passages go in big batches."""
    enc = tok([t for _, t in chunk], truncation=True, max_length=MAX_LEN)["input_ids"]
    order = sorted(range(len(chunk)), key=lambda i: -len(enc[i]))
    k = 0
    while k < len(order):
        n = max(64, min(1024, BUDGET // max(len(enc[order[k]]), 8)))
        sel = order[k:k + n]
        yield np.array([chunk[i][0] for i in sel], dtype=np.int64), [enc[i] for i in sel]
        k += n


def embed_stream(path, encode, tok, on_batch, after_pid=-1, on_chunk=None):
    """Embeds every passage of a shard after after_pid, calling on_batch(pids, vectors) per batch and
    on_chunk(last pid) after each pid-contiguous chunk (a safe point to checkpoint); returns the count."""
    chunk, total = [], 0
    for item in passages(path, after_pid):
        chunk.append(item)
        if len(chunk) == CHUNK:
            total += flush(chunk, encode, tok, on_batch)
            if on_chunk:
                on_chunk(chunk[-1][0])
            chunk = []
    if chunk:
        total += flush(chunk, encode, tok, on_batch)
        if on_chunk:
            on_chunk(chunk[-1][0])
    return total


def flush(chunk, encode, tok, on_batch):
    # Heat is handled by bench/heatwatch.sh around the process: no sleeps in here.
    n = 0
    for pids, ids in batches(chunk, tok):
        on_batch(pids, encode(ids).cpu().numpy())
        n += len(pids)
    return n


def embed_overlapped(path, model, tok, index, centres, shard_no, on_added, after_pid=-1, on_chunk=None):
    """The build loop with the CPU and GPU working at the same time (measured 2026-10-19 on 60,000
    passages: 2,622/s against 1,608/s for the serial loop, with 44% less CPU work per passage, and
    identical vectors and list choices). A helper thread reads and tokenises the next chunk while
    the GPU embeds this one; the GPU also picks each vector's IVF list (the hottest CPU part of an
    add: 16,384 centres per vector); and the CPU adds the previous batch while the GPU embeds the
    next, through pinned buffers and one CUDA event per batch so the wait is only for that batch.
    on_added(n) runs after every batch lands in the index; on_chunk(last pid) after a whole chunk
    is in the index (a safe checkpoint). Returns the count."""
    import queue
    import threading
    import faiss
    import torch
    qd = queue.Queue(maxsize=2)

    def producer():
        try:
            chunk = []
            for item in passages(path, after_pid):
                chunk.append(item)
                if len(chunk) == CHUNK:
                    qd.put((list(batches(chunk, tok)), chunk[-1][0]))
                    chunk = []
            if chunk:
                qd.put((list(batches(chunk, tok)), chunk[-1][0]))
            qd.put(None)
        except BaseException as e:  # hand any failure to the main thread instead of hanging it
            qd.put(e)
    threading.Thread(target=producer, daemon=True).start()
    pad = tok.pad_token_id
    base = shard_no << 32
    pending = None
    total = 0

    def drain():
        nonlocal pending, total
        pids, vbuf, abuf, ev = pending
        pending = None
        ev.synchronize()
        ids = np.ascontiguousarray(pids + base)
        index.add_core(len(pids), faiss.swig_ptr(vbuf.numpy()), faiss.swig_ptr(ids), faiss.swig_ptr(abuf.numpy()))
        total += len(pids)
        on_added(len(pids))
    while True:
        item = qd.get()
        if item is None:
            break
        if isinstance(item, BaseException):
            raise item
        batch_list, last_pid = item
        for pids, ids_list in batch_list:
            m, n = len(ids_list), max(len(x) for x in ids_list)
            ids = np.full((m, n), pad, dtype=np.int64)
            mask = np.zeros((m, n), dtype=np.int64)
            for i, x in enumerate(ids_list):
                ids[i, :len(x)] = x
                mask[i, :len(x)] = 1
            with torch.no_grad():
                out = model(input_ids=torch.from_numpy(ids).pin_memory().cuda(non_blocking=True),
                            attention_mask=torch.from_numpy(mask).pin_memory().cuda(non_blocking=True))
                v = torch.nn.functional.normalize(out.last_hidden_state[:, 0].float(), dim=1)  # CLS pooling
                a = (v @ centres.T).argmax(1)
            vbuf = torch.empty((m, DIM), dtype=torch.float32, pin_memory=True)
            vbuf.copy_(v, non_blocking=True)
            abuf = torch.empty((m,), dtype=torch.int64, pin_memory=True)
            abuf.copy_(a, non_blocking=True)
            ev = torch.cuda.Event()
            ev.record()
            if pending:
                drain()
            pending = (pids, vbuf, abuf, ev)
        if pending:
            drain()
        if on_chunk:
            on_chunk(last_pid)
    return total


def build():
    import faiss
    import torch
    # One working fan: 12 threads of k-means or tokenising cooks the CPU (95 C and a
    # power cut on 2026-10-18). Cap the CPU side; the GPU does the real work.
    threads = int(os.environ.get("BUILD_THREADS", "4"))
    faiss.omp_set_num_threads(threads)
    torch.set_num_threads(threads)
    os.makedirs(OUT, exist_ok=True)
    prog = json.load(open(PROGRESS)) if os.path.exists(PROGRESS) else {"done": [], "counts": {}}
    tok, encode, model = encoder_ids()
    todo = [(no, p) for no, p in shards(prog) if str(no) not in prog["done"]]
    total = sum(sqlite3.connect(f"file:{p}?mode=ro", uri=True).execute("SELECT count(*) FROM passages").fetchone()[0]
                for _, p in todo)
    bar = Task("wiki-vectors", "Corpus vectors: build", total=total)
    if os.path.exists(INDEX):
        index = faiss.read_index(INDEX)
    else:
        # Train on the first TRAIN_N passages of the first (smallest) shard, kept in RAM.
        no, path = todo[0]
        keep, kept_ids = [], []

        def collect(pids, v):
            keep.append(v.astype(np.float32))
            kept_ids.append(pids)
        chunk, got = [], 0
        for item in passages(path):
            chunk.append(item)
            if len(chunk) == CHUNK:
                got += flush(chunk, encode, tok, collect)
                chunk = []
                bar.step(CHUNK, detail="training sample")
            if got >= TRAIN_N:
                break
        x = np.concatenate(keep)
        print(f"training IVF-PQ on {len(x):,} vectors", flush=True)
        t0 = time.time()
        quant = faiss.IndexFlatIP(DIM)
        index = faiss.IndexIVFPQ(quant, DIM, NLIST, M, NBITS, faiss.METRIC_INNER_PRODUCT)
        index.train(x)
        print(f"trained in {(time.time() - t0) / 60:.1f} min", flush=True)
        ids = np.concatenate(kept_ids) + (no << 32)
        index.add_with_ids(x, ids)
        del keep, kept_ids, x
        # The rest of that shard continues below from the passage after the sample.
        prog["resume_after"] = {"shard": no, "pid": int(ids[-1] - (no << 32))}
        save(index, prog)
    # The IVF list centres on the GPU: picking each vector's list there instead of on the CPU
    # (16,384 centres per vector) removes the hottest part of the add step.
    quant = faiss.downcast_index(faiss.extract_index_ivf(index).quantizer)
    centres = torch.from_numpy(quant.reconstruct_n(0, quant.ntotal)).cuda()
    for no, path in todo:
        t0 = time.time()
        count = 0
        skip_pid = prog.get("resume_after", {}).get("pid", -1) if prog.get("resume_after", {}).get("shard") == no else -1
        if no == SIMPLE_ID and os.path.exists(f"{vec_pilot.VEC}/{vec_pilot.PACK}.npy"):
            v = np.load(f"{vec_pilot.VEC}/{vec_pilot.PACK}.npy", mmap_mode="r")
            pids = np.load(f"{vec_pilot.VEC}/{vec_pilot.PACK}.pids.npy")
            for k in range(0, len(pids), 200_000):
                index.add_with_ids(np.ascontiguousarray(v[k:k + 200_000], dtype=np.float32), pids[k:k + 200_000] + (no << 32))
            count = len(pids)
            bar.skip(count, detail="simple (pilot vectors)")
        else:
            if skip_pid >= 0:  # already in the index from the training sample: count them, do not redo them
                bar.skip(sqlite3.connect(f"file:{path}?mode=ro", uri=True)
                         .execute("SELECT count(*) FROM passages WHERE pid <= ?", (skip_pid,)).fetchone()[0],
                         detail=f"shard {no} (resuming)")

            saved_at = 0

            def added(n, no=no):
                nonlocal count
                count += n
                bar.step(n, detail=f"shard {no}")

            def checkpoint(last_pid, no=no):  # every CKPT passages: a stop or a crash loses minutes, not an hour
                nonlocal saved_at
                if count - saved_at >= CKPT:
                    prog["resume_after"] = {"shard": no, "pid": int(last_pid)}
                    save(index, prog)
                    saved_at = count
                    print(f"  checkpoint: shard {no} up to pid {last_pid}, index {index.ntotal:,}", flush=True)
            embed_overlapped(path, model, tok, index, centres, no, added, after_pid=skip_pid, on_chunk=checkpoint)
        prog["done"].append(str(no))
        prog["counts"][str(no)] = prog["counts"].get(str(no), 0) + count
        prog.pop("resume_after", None)
        save(index, prog)
        print(f"shard {no}: {count:,} added in {(time.time() - t0) / 60:.0f} min; index now {index.ntotal:,}", flush=True)
    bar.finish()
    json.dump({"model": "BAAI/bge-small-en-v1.5", "dim": DIM, "index": f"IVF{NLIST},PQ{M}x{NBITS}", "metric": "inner product",
               "query_prefix": vec_pilot.QUERY_PREFIX, "passage_text": "title: text[:900] (simple: [:1500], 256 tokens)",
               "max_len": MAX_LEN,
               "ntotal": index.ntotal, "ids": "shard_no * 2^32 + pid (shard 100 = wikipedia-simple, 101 up = progress.json packs)",
               "built": time.strftime("%Y-%m-%d %H:%M")}, open(f"{OUT}/manifest.json", "w"), indent=1)
    del encode
    torch.cuda.empty_cache()


def save(index, prog):
    import faiss
    faiss.write_index(index, INDEX + ".tmp")
    os.replace(INDEX + ".tmp", INDEX)
    json.dump(prog, open(PROGRESS + ".tmp", "w"))
    os.replace(PROGRESS + ".tmp", PROGRESS)


class Searcher:
    def __init__(self, nprobe=64):
        import faiss
        self.index = faiss.read_index(INDEX)
        self.index.nprobe = nprobe
        self.encode = vec_pilot.encoder()
        self.cons = {}

    def con(self, no):
        if no not in self.cons:
            path = shard_path(no)
            self.cons[no] = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
        return self.cons[no]

    def search(self, questions, k=20):
        q = self.encode([vec_pilot.QUERY_PREFIX + x for x in questions], 64).float().cpu().numpy()
        _, ids = self.index.search(q, k)
        out = []
        for row in ids:
            ps = []
            for i in row:
                if i < 0:
                    continue
                no, pid = int(i >> 32), int(i & 0xFFFFFFFF)
                r = self.con(no).execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id=p.doc_id "
                                         "WHERE p.pid=?", (pid,)).fetchone()
                if r:
                    ps.append(f"{r[0]}: {' '.join(r[1].split())}")
            out.append(ps)
        return out


def test():
    from evaluate import nq_items
    from qa_score import nq_right
    s = Searcher()
    sets = {"pilot 400 (Simple-answerable)": [json.loads(l) for l in open(f"{vec_pilot.VEC}/testset.jsonl")],
            "fresh NQ 300": nq_items(300, 23, 11)}
    lines = ["\n### Full index test", "| questions | found in top 5 | found in top 20 |", "|---|---|---|"]
    for name, items in sets.items():
        f5 = f20 = 0
        for k in range(0, len(items), 32):
            chunk = items[k:k + 32]
            for it, ps in zip(chunk, s.search([it["question"] for it in chunk])):
                f5 += any(nq_right(p, it["aliases"]) for p in ps[:5])
                f20 += any(nq_right(p, it["aliases"]) for p in ps)
        n = len(items)
        lines.append(f"| {name} | {f5} ({100 * f5 / n:.0f}%) | {f20} ({100 * f20 / n:.0f}%) |")
    text = "\n".join(lines)
    print(text)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md"), "a") as f:
        f.write(text + "\n")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "build":
        build()
    elif cmd == "test":
        test()
    else:
        for i, p in enumerate(Searcher().search([" ".join(sys.argv[2:3])], int(sys.argv[3]) if len(sys.argv) > 3 else 10)[0], 1):
            print(f"[{i}] {p[:200]}")
