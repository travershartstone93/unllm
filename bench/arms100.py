#!/usr/bin/env python3
"""Title coverage and article-size prior, five arms, untouched 100 (weights fixed in advance)."""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import retrieval50 as R  # noqa: E402
from taskprogress import Task  # noqa: E402

A = R.AUDIT
BASE = {"UNLLM_PACKS": f"{A}/packs-en", "UNLLM_BOOSTS": "4,0.5", "UNLLM_MIN_MATCH": "n-1"}
ARMS = [("ctl", "rerank", {}), ("wide", "rerank,wide", {}),
        ("wide-cov", "rerank,wide", {"UNLLM_W_TITLECOV": "0.15"}),
        ("wide-size", "rerank,wide", {"UNLLM_W_SIZE": "0.05"}),
        ("wide-both", "rerank,wide", {"UNLLM_W_TITLECOV": "0.15", "UNLLM_W_SIZE": "0.05"})]
NINE = ["c003", "c051", "c074", "c127", "c155", "c159", "c171", "c177", "c181"]


def load(n):
    return {json.loads(l)["id"]: json.loads(l)["passages"] for l in open(f"{A}/retr-{n}.jsonl")}


def main():
    bar = Task("arms100", "Title coverage / size arms", total=1 + 2 * len(ARMS))
    R.SET = f"{A}/fresh50.tsv"
    R.run("check-ctl", "rerank", BASE)
    a, b = load("check-ctl"), load("fresh-ctl")
    print(f"rebuilt control vs frozen control on fresh 50: identical lists {sum(a[k] == b[k] for k in a)} of {len(a)}")
    bar.step()
    for tag, path in (("r100", f"{A}/rest100.tsv"), ("f50", f"{A}/fresh50.tsv")):
        R.SET = path
        for name, layers, extra in ARMS:
            R.run(f"{tag}-{name}", layers, {**BASE, **extra})
            bar.step(detail=f"{tag}-{name}")
        print(f"\n=== {tag}")
        R.score([f"{tag}-{n}" for n, _, _ in ARMS])
        qs = R.questions()
        ok = lambda q, ps: bool(ps) and re.search(q["pattern"], ps[0], re.I) is not None  # noqa: E731
        ctl = load(f"{tag}-ctl")
        for name, _, _ in ARMS[1:]:
            run = load(f"{tag}-{name}")
            g = [q["id"] for q in qs if ok(q, run[q["id"]]) and not ok(q, ctl[q["id"]])]
            l = [q["id"] for q in qs if ok(q, ctl[q["id"]]) and not ok(q, run[q["id"]])]
            print(f"  {name}: answer first +{len(g)} -{len(l)}  gained {g}  lost {l}")
        if tag == "f50":
            src = {q["id"]: q["source"] for q in qs}
            for name, _, _ in ARMS:
                run = load(f"{tag}-{name}")
                top5 = []
                for i in NINE:
                    titles = []
                    for p in run[i]:
                        t = p.split(": ", 1)[0]
                        if t not in titles:
                            titles.append(t)
                    top5.append(any(R.key(t) == R.key(src[i]) for t in titles[:5]))
                print(f"  nine wrong-article cases, correct article in top 5 articles: {name} {sum(top5)} of 9")
    bar.finish()


if __name__ == "__main__":
    main()
