#!/usr/bin/env python3
"""The experiment for the library's own meaning search (spine audit, 2026-10-19):
  (a) its question vectors must match Python's (cosine >= 0.999 on the 400 pilot questions)
  (b) its top-20 ids must match faiss's for the same index (>= 95% of ids in common)
  (c) the plain CLI, no door, must score >= 110 right on the 400 (the door gave 113)

  meaning_check.py           runs (a) and (b); then spine_audit.py run meaning3 and score
"""

import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vec_pilot  # noqa: E402
from jobs import DATA  # noqa: E402

TOOL = os.path.expanduser("~/repo-work/unllm/build/unllm_meaning")
INDEX = os.path.expanduser("~/.local/share/unllm/vectors/wikipedia-simple.unlpq")
MODEL = os.path.expanduser("~/.local/share/unllm/models/bge-small-en-v1.5")
REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md")


def main():
    items = vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")
    ref_v = np.load(f"{DATA}/audit/ref-vectors.npy")
    ref_ids = json.load(open(f"{DATA}/audit/ref-top20.json"))
    r = subprocess.run([TOOL, INDEX, MODEL, "20", "64"], input="".join(it["question"] + "\n" for it in items),
                       capture_output=True, text=True, timeout=600)
    rows = [json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")]
    if len(rows) != len(items):
        sys.exit(f"tool gave {len(rows)} rows: {r.stderr[:300]}")
    cos = np.array([float(np.dot(np.array(x["vector"]), ref_v[i])) for i, x in enumerate(rows)])
    overlap = np.array([len(set(x["ids"]) & set(ref_ids[it["id"]])) / 20 for it, x in zip(items, rows)])
    exact = sum(1 for it, x in zip(items, rows) if x["ids"] == ref_ids[it["id"]])
    e_ms = np.median([x["embed_ms"] for x in rows])
    s_ms = np.median([x["search_ms"] for x in rows])
    lines = [f"(a) vectors: cosine to Python min {cos.min():.4f} mean {cos.mean():.4f}; {'PASS' if cos.min() >= 0.999 else 'FAIL'} (mark 0.999)",
             f"(b) top 20 against faiss at nprobe 64: ids in common mean {100 * overlap.mean():.1f}%, identical lists {exact} of {len(items)}; "
             f"{'PASS' if overlap.mean() >= 0.95 else 'FAIL'} (mark 95%)",
             f"    per question: embed {e_ms:.1f} ms, search {s_ms:.1f} ms (median, one process)"]
    print("\n".join(lines))
    with open(REPORT, "a") as f:
        f.write("\n### The library's own meaning search (C++), 2026-10-19\n" + "".join(f"- {l}\n" for l in lines))


if __name__ == "__main__":
    main()
