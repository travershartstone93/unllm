#!/usr/bin/env python3
"""Where does search lose the answer? For every NQ300 question whose answer is
not in the 20 passages unllm found, look for the answer text in every pack.

  search_check.py

Each miss is sorted into one bucket:
  absent        the answer text appears in no passage at all (corpus gap)
  same article  it appears in an article unllm did rank in its top 20, just
                not in the passage it chose (passage ranking)
  topical       it appears in an article whose title shares a content word
                with the question (keyword search could have found it: ranking)
  lexical       it appears in a passage holding 2+ of the question's content
                words, but in an article whose title shares none (weaker)
  weak          it appears with only 1 question word nearby
  semantic      it appears, but never near any of the question's words
                (only meaning-based search could find it)
The pack index has no phrase search, so an AND query over the answer's words
is confirmed with an exact whole-word match in Python. Common answers (years,
single words) match thousands of passages; the question-word query bounds
that. Writes DATA/search-check.jsonl and prints the counts; tray bar.
"""

import glob
import json
import os
import re
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from evaluate import nq_items  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right  # noqa: E402
from taskprogress import Task  # noqa: E402

PACKS = os.path.expanduser("~/.local/share/unllm/packs")
STOP = set("""the a an of in on at to and or for is was were by with from as its it his her their who what
when where which how did do does are be been has have had this that these those there than then into
over under about after before during first last many much more most name called also one two three
year years""".split())
LIMIT = 300


def words(text):
    return [w for w in re.findall(r"[a-z0-9]+", text.lower())]


def content(text):
    return [w for w in words(text) if w not in STOP and len(w) >= 3]


def overlap(qwords, text):
    low = text.lower()
    return sum(1 for w in set(qwords) if re.search(r"\b" + re.escape(w) + r"s?\b", low))


def fts(terms):
    return " AND ".join(f'"{t}"' for t in terms)


def main():
    items = nq_items(300, 11, None)
    cache = {}
    for line in open(f"{DATA}/sets/pcache-nq.jsonl"):
        c = json.loads(line)
        cache[c["question"]] = c["reply"]["passages"]
    shards = sorted(glob.glob(f"{PACKS}/*/shard-*.sqlite"))
    conns = [(os.path.basename(os.path.dirname(s)), sqlite3.connect(f"file:{s}?mode=ro", uri=True)) for s in shards]
    misses = []
    for it in items:
        ps = cache.get(it["question"], [])[:20]
        if ps and not any(nq_right(p, it["aliases"]) for p in ps):
            misses.append((it, ps))
    print(f"{len(items)} questions, {len(misses)} with the answer missing from the top 20 passages", flush=True)
    bar = Task("searchcheck", "Search check: where is the answer?", total=len(misses))
    buckets = {k: 0 for k in ("absent", "same article", "topical", "lexical", "weak", "semantic")}
    with open(f"{DATA}/search-check.jsonl", "w") as out:
        for it, ps in misses:
            top_titles = {p.split(": ", 1)[0] for p in ps}
            qw = content(it["question"])
            exists, best, hits = False, {"overlap": -1, "title_overlap": -1}, 0
            for alias in it["aliases"]:
                aw = words(alias)
                if not aw:
                    continue
                q1 = fts(aw)
                q2 = fts(aw) + " AND (" + " OR ".join(f'"{w}"' for w in qw) + ")" if qw else q1
                for pack, con in conns:
                    for q, tag in ((q2, "near"), (q1, "any")):
                        try:
                            rows = con.execute(
                                "SELECT d.title, p.text FROM passages_fts f JOIN passages p ON p.pid=f.rowid "
                                "JOIN docs d ON d.doc_id=p.doc_id WHERE passages_fts MATCH ? LIMIT ?", (q, LIMIT)).fetchall()
                        except sqlite3.OperationalError:
                            rows = []
                        for title, text in rows:
                            if not nq_right(text, [alias]):
                                continue
                            exists = True
                            hits += 1
                            tov, ov = overlap(qw, title), overlap(qw, text)
                            if (tov, ov) > (best["title_overlap"], best["overlap"]):
                                best = {"overlap": ov, "title_overlap": tov, "title": title, "pack": pack,
                                        "same": title in top_titles, "text": " ".join(text.split())[:200], "alias": alias}
                        if tag == "near" and best["overlap"] >= 1:
                            break  # the question-word query found it; no need for the unbounded one
            if not exists:
                bucket = "absent"
            elif best.get("same"):
                bucket = "same article"
            elif best["title_overlap"] >= 1:
                bucket = "topical"
            elif best["overlap"] >= 2:
                bucket = "lexical"
            elif best["overlap"] == 1:
                bucket = "weak"
            else:
                bucket = "semantic"
            buckets[bucket] += 1
            out.write(json.dumps({"id": it["id"], "question": it["question"], "aliases": it["aliases"],
                                  "bucket": bucket, "best": best, "hits": hits, "top_titles": sorted(top_titles)}) + "\n")
            out.flush()
            bar.step(detail=bucket)
    bar.finish()
    print(f"misses {len(misses)}: " + ", ".join(f"{k} {v}" for k, v in buckets.items()))


if __name__ == "__main__":
    main()
