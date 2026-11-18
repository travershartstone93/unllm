#!/usr/bin/env python3
"""Reference answers from Claude Haiku (claude -p --model haiku), the
foundation every small model is scored against.

  gold.py [JOB ...]          label every item (all jobs by default)
  gold.py judge A.jsonl      (used by score.py) same-meaning judgments

Items go 15 to a call; answers outside a job's menu are asked again once.
Saves DATA/gold/<job>.jsonl as it goes and resumes; tray progress bar.
"""

import concurrent.futures
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from jobs import DATA, JOBS  # noqa: E402
from taskprogress import Task  # noqa: E402

BATCH = 15


def haiku(text, timeout=300):
    r = subprocess.run(["claude", "-p", "--model", "haiku", text], capture_output=True, text=True, timeout=timeout,
                       cwd=os.path.join(DATA, "gold"))
    return r.stdout


def parse_list(out):
    m = re.search(r"\[.*\]", out, re.S)
    if not m:
        return []
    try:
        return json.loads(m.group(0))
    except json.JSONDecodeError:
        return []


def batch_prompt(job, batch):
    """The labelling prompt for one batch of items (shared with bench/api_run.py)."""
    spec = JOBS[job]
    parts = [f"You are labelling test items. Do not use any tools. Task: {spec['instruction']}", ""]
    for it in batch:
        parts.append(f"=== ITEM {it['id']} ===\n{it['input']}")
        if it.get("choices"):
            parts.append("Choices: " + " | ".join(it["choices"]))
        parts.append("")
    if spec.get("written"):
        parts.append('Reply with only a JSON array, one object per item: [{"id": "...", "answer": "..."}]')
    else:
        parts.append('Reply with only a JSON array, one object per item, each answer copied exactly from that '
                     'item\'s choices: [{"id": "...", "answer": "..."}]')
    return "\n".join(parts)


def label_batch(job, batch):
    spec = JOBS[job]
    got = {str(o.get("id")): o.get("answer") for o in parse_list(haiku(batch_prompt(job, batch))) if isinstance(o, dict)}
    out = []
    for it in batch:
        a = got.get(it["id"])
        ok = isinstance(a, str) and (spec.get("written") or a in it["choices"])
        out.append((it, a if ok else None))
    return out


def label(job):
    items = [json.loads(l) for l in open(f"{DATA}/sets/items-{job}.jsonl")]
    path = f"{DATA}/gold/{job}.jsonl"
    done = {}
    if os.path.exists(path):
        done = {json.loads(l)["id"]: 1 for l in open(path)}
    todo = [it for it in items if it["id"] not in done]
    return items, todo, path


def main():
    os.makedirs(DATA + "/gold", exist_ok=True)
    jobs = sys.argv[1:] or list(JOBS)
    plan = {j: label(j) for j in jobs}
    bar = Task("gold", "Haiku reference answers", total=sum(len(p[0]) for p in plan.values()))
    bar.skip(sum(len(p[0]) - len(p[1]) for p in plan.values()))
    for job, (items, todo, path) in plan.items():
        batches = [todo[i:i + BATCH] for i in range(0, len(todo), BATCH)]
        with open(path, "a") as f, concurrent.futures.ThreadPoolExecutor(3) as ex:
            for res in ex.map(lambda b: label_batch(job, b), batches):
                retry = [it for it, a in res if a is None]
                if retry:  # once more, one at a time
                    res = [(it, a) for it, a in res if a is not None] + [label_batch(job, [it])[0] for it in retry]
                for it, a in res:
                    f.write(json.dumps({"id": it["id"], "answer": a}) + "\n")
                f.flush()
                bar.step(len(res), detail=job)
    bar.finish()


if __name__ == "__main__":
    main()
