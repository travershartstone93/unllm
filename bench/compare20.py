#!/usr/bin/env python3
"""The user's benchmark: can unllm explain the difference between a mutex, a spinlock and a semaphore, in any
combination. 20 one-shot questions (DATA/audit/compare20.tsv), each naming the pairs it asks about. Opus 5.5
judges each answer: for every pair asked, does the answer state a true property that distinguishes the two
(ownership or sleeping for a mutex, busy-waiting for a spinlock, counting or signalling for a semaphore)?
A question passes only if every pair it asks about is covered. Rows -> DATA/audit/compare20-LABEL.jsonl.
  compare20.py LABEL"""
import json, os, re, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
GOLD = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/gold")
CLI = os.environ.get("UNLLM_CLI") or os.path.expanduser("~/repo-work/unllm/build/unllm")
JUDGE = """You are judging answers from an assistant that must explain the difference between a mutex, a spinlock and a semaphore. For each item, the question names the pairs it asks about. For EACH pair, decide whether the answer states at least one TRUE property that distinguishes the two (for example: a mutex has an owner and only the locker unlocks it, or puts the waiter to sleep; a spinlock busy-waits in a loop and never sleeps; a semaphore has a count and can be signalled by another task). An answer that describes only one of the two, or is a code fragment with no stated difference, fails that pair. An answer that states something false fails.

Items:
{items}

Reply with only a JSON list: [{{"item": N, "pairs": {{"mutex-spinlock": true or false, ...}}}}, ...] with exactly the pairs each item asked about."""


def main(label):
    rows = [dict(zip(("id", "question", "pairs"), l.split("\t"))) for l in open(f"{A}/compare20.tsv").read().splitlines()[1:]]
    bar = Task("compare20", f"Mutex, spinlock, semaphore: {label}", total=len(rows))
    for r in rows:
        out = subprocess.run([CLI, "--branch", r["question"]], capture_output=True, text=True, errors="replace", timeout=1800).stdout
        r["answer"] = " ".join(out.split("\n\n")[0].split())
        r["branch"] = next((l[9:-1] for l in out.splitlines() if l.startswith("[branch: ")), "")
        bar.step(detail=r["id"])
    bar.finish()
    items = "\n\n".join(f"[{i}] question: {r['question']}\npairs asked: {r['pairs']}\nanswer: {r['answer'][:1200]}" for i, r in enumerate(rows, 1))
    raw = subprocess.run(["claude", "-p", "--model", "claude-opus-5-5", JUDGE.format(items=items)], capture_output=True, text=True,
                         timeout=900, stdin=subprocess.DEVNULL, cwd=GOLD).stdout
    m = re.search(r"\[.*\]", raw, re.S)
    verdicts = {int(v["item"]): v.get("pairs", {}) for v in (json.loads(m.group(0)) if m else []) if isinstance(v, dict)}
    passed = 0
    for i, r in enumerate(rows, 1):
        v = verdicts.get(i, {})
        r["judged"] = v
        r["pass"] = all(bool(v.get(p)) for p in r["pairs"].split(","))
        passed += r["pass"]
    with open(f"{A}/compare20-{label}.jsonl", "w") as f:
        f.writelines(json.dumps(r) + "\n" for r in rows)
    lines = [f"\n### Mutex, spinlock, semaphore: {label} ({time.strftime('%Y-%m-%d %H:%M')})",
             f"- {passed} of 20 questions explain every pair they ask about (Opus judged); pairs right "
             f"{sum(sum(bool(x) for x in r['judged'].values()) for r in rows)} of {sum(len(r['pairs'].split(',')) for r in rows)}"]
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
