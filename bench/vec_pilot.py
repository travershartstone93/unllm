#!/usr/bin/env python3
"""Vector search pilot on one shard: does meaning-based search find answers
that unllm's word search misses? Everything runs on the same shard and the
same questions, so the two are compared like for like.

  vec_pilot.py testset        NQ validation questions (outside the 300 used for
                              tuning) whose answer is in the shard near the
                              question's words; capped at 400
  vec_pilot.py word           unllm's own search, restricted to the shard
  vec_pilot.py embed          every passage of the shard -> bge-small-en-v1.5
                              vectors (GPU, fp16), saved as a memmap
  vec_pilot.py search         each question -> its 20 closest passages
  vec_pilot.py score          found-in-top-20 / top-5 for word, vector, union

Files live in DATA/vec/. Resumable where it matters; tray bars.
"""

import glob
import json
import os
import random
import re
import sqlite3
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from evaluate import nq_items  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right, run_unllm  # noqa: E402
from recall import cool_down  # noqa: E402
from taskprogress import Task  # noqa: E402

PACK = "wikipedia-simple"
SHARD = os.path.expanduser(f"~/.local/share/unllm/packs/{PACK}/shard-000.sqlite")
MODEL = os.path.expanduser("~/Downloads/unllm-models/hf/bge-small-en-v1.5")
BINARY = os.path.expanduser("~/repo-work/unllm/build/unllm")
VEC = f"{DATA}/vec"
QUERY_PREFIX = "Represent this sentence for searching relevant passages: "
STOP = set("""the a an of in on at to and or for is was were by with from as its it his her their who what
when where which how did do does are be been has have had this that these those there than then into
over under about after before during first last many much more most name called also one two three
year years""".split())


def words(text):
    return re.findall(r"[a-z0-9]+", text.lower())


def content(text):
    return [w for w in words(text) if w not in STOP and len(w) >= 3]


def overlap(qwords, text):
    low = text.lower()
    return sum(1 for w in set(qwords) if re.search(r"\b" + re.escape(w) + r"s?\b", low))


def shard():
    return sqlite3.connect(f"file:{SHARD}?mode=ro", uri=True)


def load(path):
    return [json.loads(l) for l in open(path)] if os.path.exists(path) else []


def testset():
    import pyarrow.parquet as pq
    os.makedirs(VEC, exist_ok=True)
    burnt = {it["question"] for it in nq_items(300, 11, None)}
    rows = [r for r in pq.read_table(os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet")).to_pylist()
            if r["question"] not in burnt]
    con = shard()
    bar = Task("vec-testset", "Vector pilot: test questions", total=len(rows))
    keep = []
    for r in rows:
        bar.step()
        qw = content(r["question"])
        if not qw:
            continue
        ok = False
        for alias in r["answer"]:
            aw = words(alias)
            if not aw:
                continue
            q = " AND ".join(f'"{t}"' for t in aw) + " AND (" + " OR ".join(f'"{w}"' for w in qw) + ")"
            try:
                hits = con.execute("SELECT d.title, p.text FROM passages_fts f JOIN passages p ON p.pid=f.rowid "
                                   "JOIN docs d ON d.doc_id=p.doc_id WHERE passages_fts MATCH ? LIMIT 200", (q,)).fetchall()
            except sqlite3.OperationalError:
                hits = []
            if any(nq_right(t, [alias]) and (overlap(qw, t) >= 2 or overlap(qw, title) >= 1) for title, t in hits):
                ok = True
                break
        if ok:
            keep.append({"question": r["question"], "aliases": r["answer"]})
    bar.finish()
    random.Random(5).shuffle(keep)
    keep = keep[:400]
    with open(f"{VEC}/testset.jsonl", "w") as f:
        for i, it in enumerate(keep):
            it["id"] = f"v{i:03d}"
            f.write(json.dumps(it) + "\n")
    print(f"{len(rows)} candidate questions, {len(keep)} kept (answer in the shard near the question's words)")


def word():
    """unllm's search on this shard only: a packs folder holding just this pack."""
    import concurrent.futures
    packs = f"{VEC}/packs-{PACK}"
    os.makedirs(packs, exist_ok=True)
    link = f"{packs}/{PACK}"
    if not os.path.exists(link):
        os.symlink(os.path.dirname(SHARD), link)
    os.environ["UNLLM_PACKS"] = packs
    items = load(f"{VEC}/testset.jsonl")
    path = f"{VEC}/word.jsonl"
    done = {r["id"] for r in load(path)}
    todo = [it for it in items if it["id"] not in done]
    bar = Task("vec-word", "Vector pilot: unllm word search", total=len(items))
    bar.skip(len(done))

    def one(it):
        cool_down()
        return it, run_unllm(BINARY, it["question"], passages=20)
    with open(path, "a") as f, concurrent.futures.ThreadPoolExecutor(2) as ex:
        for it, reply in ex.map(one, todo):
            f.write(json.dumps({"id": it["id"], "passages": reply["passages"][:20], "answer": reply["answer"][:300]}) + "\n")
            f.flush()
            bar.step()
    bar.finish()


def encoder():
    import torch
    from transformers import AutoModel, AutoTokenizer
    tok = AutoTokenizer.from_pretrained(MODEL)
    model = AutoModel.from_pretrained(MODEL, dtype=torch.float16).cuda().eval()

    @torch.no_grad()
    def encode(texts, max_length):
        enc = tok(texts, truncation=True, max_length=max_length, padding=True, return_tensors="pt").to("cuda")
        out = model(**enc).last_hidden_state[:, 0]  # CLS pooling, as bge recommends
        return torch.nn.functional.normalize(out.float(), dim=1).half()
    return encode


def embed():
    import torch
    os.makedirs(VEC, exist_ok=True)
    con = shard()
    n = con.execute("SELECT count(*) FROM passages").fetchone()[0]
    rows = con.execute("SELECT p.pid, d.title, p.text FROM passages p JOIN docs d ON d.doc_id=p.doc_id ORDER BY p.pid")
    encode = encoder()
    vecs = np.lib.format.open_memmap(f"{VEC}/{PACK}.npy", mode="w+", dtype=np.float16, shape=(n, 384))
    pids = np.zeros(n, dtype=np.int64)
    bar = Task("vec-embed", f"Vector pilot: embedding {PACK}", total=n)
    i, batch = 0, []

    def flush():
        nonlocal i
        texts = [f"{t}: {' '.join(x.split())[:1500]}" for _, t, x in batch]
        v = encode(texts, 256).cpu().numpy()
        vecs[i:i + len(batch)] = v
        pids[i:i + len(batch)] = [p for p, _, _ in batch]
        i += len(batch)
        bar.step(len(batch))
        batch.clear()
    for row in rows:
        batch.append(row)
        if len(batch) == 256:
            flush()
    if batch:
        flush()
    vecs.flush()
    np.save(f"{VEC}/{PACK}.pids.npy", pids)
    bar.finish()
    print(f"embedded {i} passages")


def search():
    import torch
    vecs = torch.from_numpy(np.load(f"{VEC}/{PACK}.npy", mmap_mode="r")[:]).cuda()
    pids = np.load(f"{VEC}/{PACK}.pids.npy")
    items = load(f"{VEC}/testset.jsonl")
    encode = encoder()
    con = shard()
    with open(f"{VEC}/vector.jsonl", "w") as f:
        for k in range(0, len(items), 32):
            chunk = items[k:k + 32]
            q = encode([QUERY_PREFIX + it["question"] for it in chunk], 64)
            top = torch.topk(q @ vecs.T, 20, dim=1).indices.cpu().numpy()
            for it, idx in zip(chunk, top):
                ps = []
                for j in idx:
                    title, text = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id=p.doc_id "
                                              "WHERE p.pid=?", (int(pids[j]),)).fetchone()
                    ps.append(f"{title}: {' '.join(text.split())}")
                f.write(json.dumps({"id": it["id"], "passages": ps}) + "\n")
    print(f"searched {len(items)} questions")


def score():
    items = {it["id"]: it for it in load(f"{VEC}/testset.jsonl")}
    w = {r["id"]: r["passages"] for r in load(f"{VEC}/word.jsonl")}
    v = {r["id"]: r["passages"] for r in load(f"{VEC}/vector.jsonl")}
    ids = [i for i in items if i in w and i in v]

    def found(ps, al):
        return any(nq_right(p, al) for p in ps)
    lines = [f"| search | found in top 5 | found in top 20 | (n={len(ids)}) |", "|---|---|---|---|"]
    for name, lists in (("word (unllm)", {i: w[i] for i in ids}), ("vector (bge-small)", {i: v[i] for i in ids}),
                        ("union (10 + 10)", {i: w[i][:10] + [p for p in v[i][:10] if p not in w[i][:10]] for i in ids}),
                        ("either (20 + 20)", {i: w[i] + v[i] for i in ids})):
        f5 = sum(found(lists[i][:5], items[i]["aliases"]) for i in ids)
        f20 = sum(found(lists[i][:20] if "either" not in name else lists[i], items[i]["aliases"]) for i in ids)
        lines.append(f"| {name} | {f5} ({100 * f5 / len(ids):.0f}%) | {f20} ({100 * f20 / len(ids):.0f}%) | |")
    both = sum(found(w[i], items[i]["aliases"]) and found(v[i], items[i]["aliases"]) for i in ids)
    only_w = sum(found(w[i], items[i]["aliases"]) and not found(v[i], items[i]["aliases"]) for i in ids)
    only_v = sum(found(v[i], items[i]["aliases"]) and not found(w[i], items[i]["aliases"]) for i in ids)
    lines.append(f"\nTop 20: both find it {both}, only word {only_w}, only vector {only_v}.")
    text = "\n".join(lines)
    print(text)
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/vector-pilot.md"), "a") as f:
        f.write("\n## Result\n" + text + "\n")


if __name__ == "__main__":
    {"testset": testset, "word": word, "embed": embed, "search": search, "score": score}[sys.argv[1]]()
