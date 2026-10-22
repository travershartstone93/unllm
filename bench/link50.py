#!/usr/bin/env python3
"""Spine experiment without vectors (2026-10-19): link the question to the article it names
(longest question phrase that is an article title), then read inside that article (fact-box
row or passage sharing the most remaining question words). On the verified 50
(DATA/audit/dev50.tsv). Marks: linked article = source article >= 42; chosen unit holds the
answer >= 35 (word search: 25)."""

import glob
import os
import re
import sqlite3

PACKS = os.path.expanduser("~/.local/share/unllm/packs")
SET = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit/dev50.tsv")
FILL = set("""a an the of in on at to for by with from and or is are was were be been do does did what which who whom
whose when where why how many much year years name named called first last largest biggest deepest
type kind company tv show date""".split())
QW = FILL | set("there their its it this that".split())


def key(s):
    t = re.sub(r"[^0-9a-z]+", " ", s.lower()).strip()
    for a in ("the ", "a ", "an "):
        if t.startswith(a):
            return t[len(a):]
    return t


def stem(w):
    return w[:-1] if len(w) > 4 else w


def main():
    shards = [sqlite3.connect(f"file:{p}?mode=ro", uri=True) for p in
              sorted(glob.glob(f"{PACKS}/wikipedia-en/shard-*.sqlite")) + [f"{PACKS}/wikipedia-simple/shard-000.sqlite"]]
    rows = [l.split("\t") for l in open(SET).read().splitlines()[1:]]
    linked_ok = unit_ok = in_article = 0
    for qid, q, pat, typ, src in rows:
        words = re.findall(r"[a-z0-9]+(?:'[a-z]+)?", q.lower())
        best = None  # (n words, passage count, shard, doc_id, title, phrase words)
        for n in range(min(6, len(words)), 0, -1):
            for i in range(len(words) - n + 1):
                ph = words[i:i + n]
                if ph[0] in FILL or ph[-1] in FILL or all(w in FILL for w in ph):
                    continue
                k = key(" ".join(ph))
                for si, con in enumerate(shards):
                    for doc_id, title in con.execute("SELECT doc_id, title FROM docs WHERE title_key = ?", (k,)):
                        size = con.execute("SELECT count(*) FROM passages WHERE doc_id = ?", (doc_id,)).fetchone()[0]
                        cand = (n, size, si, doc_id, title, ph)
                        if best is None or cand[:2] > best[:2]:
                            best = cand
            if best:
                break
        if not best:
            print(f"  {qid} NO TITLE | {q}")
            continue
        _, _, si, doc_id, title, ph = best
        con = shards[si]
        if key(title) == key(src):
            linked_ok += 1
        rest = [w for w in words if w not in QW and w not in ph]
        units = [(f"{k}: {v}", -1) for k, v in con.execute("SELECT key, value FROM infobox WHERE doc_id = ?", (doc_id,))]
        units += [(t, o) for o, t in con.execute("SELECT ord, text FROM passages WHERE doc_id = ? ORDER BY ord", (doc_id,))]

        def score(u):
            low = u[0].lower()
            return sum(1 for w in rest if re.search(r"\b" + re.escape(stem(w)), low))
        ranked = sorted(units, key=lambda u: (-score(u), u[1] if u[1] >= 0 else 10**6))
        chosen = ranked[0][0] if ranked else ""
        ok = re.search(pat, chosen, re.I) is not None
        unit_ok += ok
        anywhere = any(re.search(pat, u[0], re.I) for u in units)
        in_article += anywhere
        if not ok or key(title) != key(src):
            print(f"  {qid} | {q[:45]} | linked '{title}' (source '{src}') | rest {rest} | chosen: {' '.join(chosen.split())[:90]}"
                  f" | answer in article: {anywhere}")
    n = len(rows)
    print(f"linked to the source article: {linked_ok} of {n} ({'PASS' if linked_ok >= 42 else 'FAIL'}, mark 42)")
    print(f"chosen row or passage holds the answer: {unit_ok} of {n} ({'PASS' if unit_ok >= 35 else 'FAIL'}, mark 35; word search 25)")
    print(f"answer anywhere in the linked article: {in_article} of {n}")


if __name__ == "__main__":
    main()
