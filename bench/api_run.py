#!/usr/bin/env python3
"""Run a hosted model over the Round 1 jobs through a free API, batched 15
items a request the way Haiku was run (bench/gold.py), so the result compares
with Haiku directly.

  api_run.py MODEL [--jobs a,b] [--limit N]

Backends by model name: gemma-* and gemini-* go to Google's Gemini API (key in
the private file named by GOOGLE_ENV, which must be set).
Calls go through curl -4 (this host's IPv6 is unreliable). Free tiers are slow
and flaky: every request waits at least PACE seconds, and failures retry with
growing waits. Models that think out loud are handled by taking the last JSON
array in the reply. Writes DATA/runs/api-<model>/<job>.jsonl in run.py's
format ({id, answer, valid, seconds}) and resumes; tray bar.
"""

import json
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from gold import batch_prompt  # noqa: E402
from jobs import DATA, JOBS  # noqa: E402
from taskprogress import Task  # noqa: E402

BATCH = 15
PACE = 6          # seconds between requests
TRIES = 8
MAX_OUT = 4000    # output tokens: room for a model that thinks before answering


def key(env_file, name):
    for line in open(os.path.expanduser(env_file)):
        if line.startswith(name + "="):
            return line.strip().split("=", 1)[1]
    raise SystemExit(f"no {name} in {env_file}")


def google(model, text):
    """One request to Google's v1 endpoint; returns (reply text, error or None)."""
    if "GOOGLE_ENV" not in os.environ:
        raise SystemExit("set GOOGLE_ENV to the file holding GOOGLE_API_KEY=... (keep it in the scratchpad)")
    api_key = key(os.environ["GOOGLE_ENV"], "GOOGLE_API_KEY")
    body = {"contents": [{"parts": [{"text": text}]}],
            "generationConfig": {"temperature": 0, "maxOutputTokens": MAX_OUT, "responseMimeType": "application/json"}}
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(body, f)
        path = f.name
    try:
        r = subprocess.run(["curl", "-4", "-s", "-m", "240", "-w", "\n%{http_code}",
                            f"https://generativelanguage.googleapis.com/v1/models/{model}:generateContent",
                            "-H", "x-goog-api-key: " + api_key, "-H", "Content-Type: application/json", "-d", "@" + path],
                           capture_output=True, text=True, timeout=260)
    finally:
        os.unlink(path)
    out, _, code = r.stdout.rpartition("\n")
    if code != "200":
        return "", f"HTTP {code}: {out[:160]}"
    try:
        parts = json.loads(out)["candidates"][0]["content"]["parts"]
        return "".join(p.get("text", "") for p in parts), None
    except (KeyError, IndexError, ValueError) as e:
        return "", f"bad reply: {e}: {out[:160]}"


def last_array(text):
    """The last JSON array in the text (models that think first write prose before it)."""
    end = text.rfind("]")
    while end != -1:
        start = text.rfind("[", 0, end)
        while start != -1:
            try:
                v = json.loads(text[start:end + 1])
                if isinstance(v, list):
                    return v
            except ValueError:
                pass
            start = text.rfind("[", 0, start)
        end = text.rfind("]", 0, end)
    return []


def call(model, text):
    wait = 20
    last = None
    for _ in range(TRIES):
        t0 = time.time()
        reply, err = google(model, text)
        if err is None:
            return reply, time.time() - t0
        last = err
        print(f"  retry in {wait}s: {err}", flush=True)
        time.sleep(wait)
        wait = min(wait * 2, 300)
    raise RuntimeError(f"gave up: {last}")


def main():
    args = sys.argv[1:]
    jobs = list(JOBS)
    limit = None
    if "--jobs" in args:
        i = args.index("--jobs")
        jobs = args[i + 1].split(",")
        del args[i:i + 2]
    if "--limit" in args:
        i = args.index("--limit")
        limit = int(args[i + 1])
        del args[i:i + 2]
    model = args[0]
    items = {j: [json.loads(l) for l in open(f"{DATA}/sets/items-{j}.jsonl")][:limit] for j in jobs}
    out_dir = f"{DATA}/runs/api-{model}"
    os.makedirs(out_dir, exist_ok=True)
    bar = Task("api-run", f"API run: {model}", total=sum(len(v) for v in items.values()))
    for job in jobs:
        path = f"{out_dir}/{job}.jsonl"
        done = {json.loads(l)["id"] for l in open(path)} if os.path.exists(path) else set()
        todo = [it for it in items[job] if it["id"] not in done]
        bar.skip(len(items[job]) - len(todo))
        with open(path, "a") as f:
            for k in range(0, len(todo), BATCH):
                batch = todo[k:k + BATCH]
                reply, secs = call(model, batch_prompt(job, batch))
                got = {str(o.get("id")): o.get("answer") for o in last_array(reply) if isinstance(o, dict)}
                for it in batch:
                    a = got.get(it["id"])
                    ok = isinstance(a, str) and (JOBS[job].get("written") or a in it["choices"])
                    f.write(json.dumps({"id": it["id"], "answer": a if isinstance(a, str) else None, "valid": bool(ok),
                                        "seconds": round(secs / len(batch), 3)}) + "\n")
                f.flush()
                bar.step(len(batch), detail=f"{job} {len(got)}/{len(batch)} parsed")
                time.sleep(PACE)
    bar.finish()


if __name__ == "__main__":
    main()
