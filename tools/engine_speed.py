#!/usr/bin/env python3
"""Search engine speed test on one English Wikipedia shard: the same question
words (an OR query, stop words removed, top 20 by bm25) through SQLite FTS5,
Tantivy and Xapian, timed, with how often the answer is in the top 20.

  engine_speed.py build tantivy|xapian [--shard 0]   index the shard's passages
  engine_speed.py run [--n 100] [--shard 0]          time every engine that has an index

Indexes go to ~/.cache/unllm-engines/<engine>-shard<N>.
"""

import argparse
import os
import random
import sqlite3
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recall import cool_down, normalize  # noqa: E402
from recall_plain import words  # noqa: E402

CACHE = os.path.expanduser("~/.cache/unllm-engines")


def shard_path(n):
    return os.path.expanduser(f"~/.local/share/unllm/packs/wikipedia-en/shard-{n:03d}.sqlite")


def passages(n):
    db = sqlite3.connect(f"file:{shard_path(n)}?mode=ro", uri=True)
    for pid, text in db.execute("SELECT pid, text FROM passages"):
        yield pid, text
    db.close()


def build_tantivy(n):
    import tantivy
    sb = tantivy.SchemaBuilder()
    sb.add_integer_field("pid", stored=True, indexed=False)
    sb.add_text_field("text", stored=False, tokenizer_name="en_stem")
    schema = sb.build()
    path = os.path.join(CACHE, f"tantivy-shard{n}")
    os.makedirs(path, exist_ok=True)
    index = tantivy.Index(schema, path=path)
    writer = index.writer(heap_size=500_000_000, num_threads=2)
    for i, (pid, text) in enumerate(passages(n), 1):
        writer.add_document(tantivy.Document(pid=pid, text=text))
        if i % 500_000 == 0:
            writer.commit()
            print(f"  {i:,} passages", flush=True)
            cool_down()
    writer.commit()
    writer.wait_merging_threads()


def build_xapian(n):
    import xapian
    path = os.path.join(CACHE, f"xapian-shard{n}")
    db = xapian.WritableDatabase(path, xapian.DB_CREATE_OR_OVERWRITE)
    tg = xapian.TermGenerator()
    tg.set_stemmer(xapian.Stem("en"))
    for i, (pid, text) in enumerate(passages(n), 1):
        doc = xapian.Document()
        tg.set_document(doc)
        tg.index_text(text)
        doc.add_value(0, xapian.sortable_serialise(pid))
        db.add_document(doc)
        if i % 500_000 == 0:
            db.commit()
            print(f"  {i:,} passages", flush=True)
            cool_down()
    db.commit()


def text_of(db, pid):
    row = db.execute("SELECT text FROM passages WHERE pid = ?", (pid,)).fetchone()
    return row[0] if row else ""


def run(n_questions, n_shard, with_sqlite=False):
    import pyarrow.parquet as pq
    rows = pq.read_table(os.path.expanduser("~/Downloads/unllm-benchmarks/nq_open/validation.parquet")).to_pylist()
    sample = random.Random(11).sample(rows, 300)[:n_questions]
    db = sqlite3.connect(f"file:{shard_path(n_shard)}?mode=ro", uri=True)
    engines = {}

    def sqlite_search(ws):
        match = " OR ".join(f'"{w}"' for w in ws)
        return [pid for (pid,) in db.execute(
            "SELECT rowid FROM passages_fts WHERE passages_fts MATCH ? ORDER BY bm25(passages_fts) LIMIT 20", (match,))]
    if with_sqlite:  # slow: seconds per question
        engines["sqlite fts5"] = sqlite_search

    tpath = os.path.join(CACHE, f"tantivy-shard{n_shard}")
    if os.path.isdir(tpath):
        import tantivy
        index = tantivy.Index.open(tpath)
        searcher = index.searcher()

        def tantivy_search(ws):
            q = index.parse_query(" OR ".join(ws), ["text"])
            return [searcher.doc(addr)["pid"][0] for _, addr in searcher.search(q, 20).hits]
        engines["tantivy"] = tantivy_search

    xpath = os.path.join(CACHE, f"xapian-shard{n_shard}")
    if os.path.isdir(xpath):
        import xapian
        xdb = xapian.Database(xpath)
        qp = xapian.QueryParser()
        qp.set_stemmer(xapian.Stem("en"))
        qp.set_stemming_strategy(xapian.QueryParser.STEM_SOME)
        qp.set_default_op(xapian.Query.OP_OR)

        def xapian_search(ws):
            enq = xapian.Enquire(xdb)
            enq.set_query(qp.parse_query(" ".join(ws)))
            return [int(xapian.sortable_unserialise(m.document.get_value(0))) for m in enq.get_mset(0, 20)]
        engines["xapian"] = xapian_search

    print(f"{n_questions} Natural Questions, one shard ({db.execute('SELECT COUNT(*) FROM passages').fetchone()[0]:,} "
          f"passages), OR query, top 20")
    for name, search in engines.items():
        times, found = [], 0
        for r in sample:
            cool_down()
            ws = words(r["question"])
            if not ws:
                continue
            t = time.time()
            pids = search(ws)
            times.append(time.time() - t)
            aliases = [normalize(a) for a in r["answer"] if normalize(a)]
            found += any(any(a in normalize(text_of(db, p)) for a in aliases) for p in pids)
        times.sort()
        print(f"  {name:<12} median {1000 * statistics.median(times):8.1f} ms   90% {1000 * times[int(0.9 * (len(times) - 1))]:8.1f} ms"
              f"   slowest {1000 * times[-1]:8.1f} ms   answer in top 20: {found} of {len(times)}", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["build", "run"])
    ap.add_argument("engine", nargs="?")
    ap.add_argument("--shard", type=int, default=0)
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--with-sqlite", action="store_true", help="also time SQLite FTS5 (slow)")
    args = ap.parse_args()
    os.makedirs(CACHE, exist_ok=True)
    if args.cmd == "build":
        t = time.time()
        {"tantivy": build_tantivy, "xapian": build_xapian}[args.engine](args.shard)
        print(f"{args.engine}: indexed shard {args.shard} in {time.time() - t:.0f} s")
    else:
        run(args.n, args.shard, args.with_sqlite)


if __name__ == "__main__":
    sys.exit(main())
