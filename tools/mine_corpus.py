#!/usr/bin/env python3
"""Mine real-world question phrasings from downloaded chat and Q&A datasets.

The datasets are read in place and never copied; only frequency lists come
out, into a local folder. Nothing this writes belongs in the repository: the
repository holds unllm's own tables, built by hand from what these lists show.

  mine_corpus.py machine    first user turns about computers (WildChat, LMSYS)
  mine_corpus.py smalltalk  the most frequent short first turns
  mine_corpus.py se-titles  titles from StackExchange top-voted JSON exports
  mine_corpus.py followups  short second-and-later user turns, ranked by how
                            many distinct conversations used each phrasing

Options: --src DIR (default ~/Downloads/unllm-corpus-sources),
         --out DIR (default SRC/mined). Needs pyarrow for the parquet sources.
"""

import argparse
import collections
import glob
import html
import json
import os
import re
import sys

MACHINE = re.compile(
    r"\b(computer|laptop|pc\b|cpu|ram\b|memory|slow|lag|freez|frozen|hot|overheat|fan\b|fans\b|"
    r"battery|crash|disk|drive|ssd|hdd|internet|wifi|network|virus|malware|hack|swap|gpu|"
    r"temperature|loud|noisy|process|task manager|boot|startup|update|install)", re.I)
QUESTIONY = re.compile(r"^(why|how|what|which|who|when|where|is|are|was|were|did|does|do|can|"
                       r"should|will|would|my|the|i\b)", re.I)
CHATSPEAK = {"u": "you", "ur": "your", "r": "are", "y": "why", "pls": "please", "plz": "please",
             "thx": "thanks", "ty": "thanks", "wat": "what", "wut": "what", "cuz": "because",
             "bc": "because", "idk": "i do not know", "rn": "right now", "2day": "today"}


def clean(s):
    return re.sub(r"\s+", " ", s).strip()


def normalize(s):
    s = s.lower()
    s = re.sub(r"[^a-z0-9' ]+", " ", s)
    words = [CHATSPEAK.get(w, w) for w in s.split()]
    return " ".join(words).strip()


def conversations(src, name):
    import pyarrow.parquet as pq
    for path in sorted(glob.glob(os.path.join(src, name, "*.parquet"))):
        pf = pq.ParquetFile(path)
        cols = [c for c in ("conversation", "language") if c in pf.schema_arrow.names]
        for batch in pf.iter_batches(batch_size=2000, columns=cols):
            for row in batch.to_pylist():
                lang = (row.get("language") or "").lower()
                if lang and not lang.startswith("engl"):
                    continue
                yield row.get("conversation") or []
        print(name, os.path.basename(path), file=sys.stderr, flush=True)


def user_turns(conv):
    out = []
    try:
        for msg in conv:
            if msg.get("role") == "user":
                out.append(clean(msg.get("content") or ""))
    except Exception:
        pass
    return out


def mine_machine(src, out, smalltalk_only=False):
    for name in ("wildchat", "lmsys"):
        machine, small = collections.Counter(), collections.Counter()
        for conv in conversations(src, name):
            turns = user_turns(conv)
            if not turns:
                continue
            turn = turns[0]
            if not turn or len(turn) > 120:
                continue
            low = turn.lower().strip(" ?!.")
            if not low.isascii():
                continue
            if len(low) <= 40:
                small[low] += 1
            if not smalltalk_only and MACHINE.search(low) and QUESTIONY.match(low) and 12 <= len(low) <= 100:
                machine[low] += 1
        if not smalltalk_only:
            with open(os.path.join(out, f"{name}-machine.txt"), "w") as f:
                for q, n in machine.most_common(3000):
                    f.write(f"{n}\t{q}\n")
        with open(os.path.join(out, f"{name}-top-smalltalk.txt"), "w") as f:
            for q, n in small.most_common(500):
                f.write(f"{n}\t{q}\n")
        print(name, "done", file=sys.stderr)


def mine_se_titles(src, out):
    with open(os.path.join(out, "se-titles.txt"), "w") as f:
        for path in sorted(glob.glob(os.path.join(src, "stackexchange", "*.json"))):
            site = os.path.basename(path).replace(".json", "")
            try:
                with open(path) as fh:
                    data = json.load(fh)
            except Exception as e:
                print("skip", path, e, file=sys.stderr)
                continue
            for item in data.get("items", []):
                title = clean(html.unescape(item.get("title", "")))
                if title:
                    f.write(f"{item.get('score', 0)}\t{site}\t{title}\n")


def mine_followups(src, out, max_words=6):
    # Counted by distinct conversation, so one user repeating "why" ten
    # times in one chat counts once.
    counts = collections.Counter()
    for name in ("wildchat", "lmsys"):
        for conv in conversations(src, name):
            turns = user_turns(conv)
            seen = set()
            for t in turns[1:]:
                if not t.isascii() or len(t) > 60:
                    continue
                n = normalize(t)
                if not n or len(n.split()) > max_words:
                    continue
                seen.add(n)
            for n in seen:
                counts[n] += 1
    with open(os.path.join(out, "followups-ranked.tsv"), "w") as f:
        for phrase, n in counts.most_common(5000):
            f.write(f"{n}\t{phrase}\n")
    print("followups done:", len(counts), "distinct phrasings", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["machine", "smalltalk", "se-titles", "followups"])
    ap.add_argument("--src", default=os.path.expanduser("~/Downloads/unllm-corpus-sources"))
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    out = a.out or os.path.join(a.src, "mined")
    os.makedirs(out, exist_ok=True)
    if a.what == "machine":
        mine_machine(a.src, out)
    elif a.what == "smalltalk":
        mine_machine(a.src, out, smalltalk_only=True)
    elif a.what == "se-titles":
        mine_se_titles(a.src, out)
    else:
        mine_followups(a.src, out)


if __name__ == "__main__":
    main()
