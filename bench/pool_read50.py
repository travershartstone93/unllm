#!/usr/bin/env python3
"""Step 1 of the spine plan: readers on the unblocked search.
  pool_read50.py save          word top 25 + vector top 25 pooled (pool50.py's raw lists), Qwen3-Reranker-0.6B
                               orders the pool, the top 10 is saved once for corpus50 and dev50
                               -> DATA/audit/pool-top10.jsonl
  pool_read50.py read quote    unllm's plain quote on the saved top 10 (passages in, every layer off)
  pool_read50.py read MODEL    an Ollama model (qwen3:4b, odysseus-stable2:8b) reads the saved top 3,
                               passages only, the rerank_4b50.py prompt
Rows -> DATA/audit/pool-read-<reader>.jsonl; totals to the audit report. Run each phase as its own
process so the reranker's GPU memory is gone before a reader model loads."""
import json, os, re, subprocess, sys, time, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md")
# POOL_SETS names the question sets (comma-separated tsv names), POOL_TAG suffixes the output files.
SETS = tuple(os.environ.get("POOL_SETS", "corpus50,dev50").split(","))
TAG = ("-" + os.environ["POOL_TAG"]) if os.environ.get("POOL_TAG") else ""


def hit(pat, text):
    return bool(text) and re.search(pat, text, re.I) is not None


def report(lines):
    print("\n".join(lines))
    with open(REPORT, "a") as f:
        f.write("\n".join(lines) + "\n")


def save():
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    import rerank
    from pool50 import SEARCHES, found
    ranker = rerank.load("qwen3-0.6b")
    rows = []
    for s in SETS:
        rows += [dict(set=s, **dict(zip(("id", "question", "pattern"), l.split("\t")[:3])))
                 for l in open(f"{A}/{s}.tsv").read().splitlines()[1:]]
    out = open(f"{A}/pool-top10{TAG}.jsonl", "w")
    tot = {s: {"top 10 holds it": 0, "top 3 holds it": 0, "top 1 holds it": 0} for s in SETS}
    bar = Task("poolsave", "Pooled search: save the top 10", total=len(rows))
    for q in rows:
        pool = list(dict.fromkeys(found(q["question"], SEARCHES["vectors"]) + found(q["question"], SEARCHES["words"])))
        sc = [float(x) for x in ranker.scores(q["question"], pool)] if pool else []
        rr = [pool[k] for k in sorted(range(len(pool)), key=lambda k: -sc[k])][:10]
        for name, n in (("top 10 holds it", 10), ("top 3 holds it", 3), ("top 1 holds it", 1)):
            tot[q["set"]][name] += any(hit(q["pattern"], p) for p in rr[:n])
        out.write(json.dumps(dict(q, pooled=len(pool), top10=rr, pool=pool)) + "\n")
        out.flush()
        bar.step(detail=f"{q['set']} {q['id']}")
    bar.finish()
    lines = [f"\n### {TAG[1:] or 'Step 1'}, saved pooled top 10 (words 25 + vectors 25, reranked) ({time.strftime('%Y-%m-%d %H:%M')})"]
    lines += [f"- {s}: " + ", ".join(f"{k} {v}" for k, v in tot[s].items()) + " of 50" for s in SETS]
    report(lines)


def quote(question, ps):
    os.makedirs(f"{A}/tmp", exist_ok=True)
    path = f"{A}/tmp/pool-read-quote.txt"
    open(path, "w").write("".join(p + "\n" for p in ps))
    env = dict(os.environ, UNLLM_PASSAGES=path, UNLLM_LAYERS="")
    out = subprocess.run([CLI, question], capture_output=True, text=True, errors="replace", env=env, timeout=120).stdout
    return " ".join(out.split("\n\n")[0].split())


def ollama(model, prompt):
    body = json.dumps({"model": model, "messages": [{"role": "user", "content": prompt}], "stream": False, "format": "json",
                       "think": False, "options": {"temperature": 0, "num_ctx": 8192}}).encode()
    req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as h:
            ans = str(json.loads(json.loads(h.read())["message"]["content"]).get("answer", "")).strip()
    except (ValueError, OSError, KeyError, TypeError):
        ans = ""
    return "" if ans == "NONE" else ans


def read(reader):
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    from rerank_4b50 import PROMPT
    rows = [json.loads(l) for l in open(f"{A}/pool-top10{TAG}.jsonl")]
    tag = reader.replace(":", "-")
    out = open(f"{A}/pool-read-{tag}{TAG}.jsonl", "w")
    tot = {s: {"passages given hold it": 0, "right": 0} for s in SETS}
    bar = Task("poolread", f"Step 1: {reader} reads the pooled top 10", total=len(rows))
    for r in rows:
        ps = r["top10"] if reader == "quote" else r["top10"][:3]
        if reader == "quote":
            ans = quote(r["question"], ps) if ps else ""
        else:
            ans = ollama(reader, PROMPT.format(q=r["question"], ps="\n\n".join(f"[{i}] {p[:900]}" for i, p in enumerate(ps, 1)))) if ps else ""
        held, ok = any(hit(r["pattern"], p) for p in ps), hit(r["pattern"], ans)
        tot[r["set"]]["passages given hold it"] += held
        tot[r["set"]]["right"] += ok
        out.write(json.dumps({"set": r["set"], "id": r["id"], "question": r["question"], "pattern": r["pattern"],
                              "held": held, "right": ok, "answer": ans}) + "\n")
        out.flush()
        bar.step(detail=f"{r['set']} {r['id']}")
    bar.finish()
    what = "plain quote on the top 10" if reader == "quote" else f"{reader} on the top 3"
    lines = [f"\n### {TAG[1:] or 'Step 1'}, reader on the pooled search: {what} ({time.strftime('%Y-%m-%d %H:%M')})"]
    lines += [f"- {s}: " + ", ".join(f"{k} {v}" for k, v in tot[s].items()) + " of 50" for s in SETS]
    report(lines)


if __name__ == "__main__":
    save() if sys.argv[1] == "save" else read(sys.argv[2])
