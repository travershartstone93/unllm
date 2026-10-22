#!/usr/bin/env python3
"""Run small models on every Round 1 job through Ollama (answers on the GPU, timing on the CPU).

  run.py MODEL [MODEL ...] [--jobs a,b] [--limit N] [--cpu]

Each reply is forced into the job's format (Ollama 'format' JSON schema;
menus as an enum), temperature 0. Answers run on the GPU for speed (the
answers do not depend on the device) and go to DATA/runs/<model>/;
--cpu runs with no GPU and 6 threads into DATA/timing/<model>/, for the
real CPU speed (use with --limit). Saves {id, answer, valid, seconds} as
it goes and resumes; unloads each model after every ~60,000 characters of prompts (llama-server's prompt cache grows in RAM) and when done; pauses above 85 C CPU
or 80 C GPU; tray bar.
"""

import json
import os
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from jobs import DATA, JOBS, prompt, schema  # noqa: E402
from recall import cool_down  # noqa: E402
from taskprogress import Task  # noqa: E402

OLLAMA = "http://127.0.0.1:11434/api"


def post(path, body, timeout=600):
    req = urllib.request.Request(OLLAMA + path, data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as f:
        return json.load(f)


def gpu_temp():
    try:
        import subprocess
        out = subprocess.run(["nvidia-smi", "--query-gpu=temperature.gpu", "--format=csv,noheader"],
                             capture_output=True, text=True, timeout=10).stdout.strip()
        return float(out) if out else None
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return None


def cool(cpu_only):
    cool_down()
    while not cpu_only and (gpu_temp() or 0) > 80:
        print(f"  gpu at {gpu_temp()} C, pausing 30 s", flush=True)
        time.sleep(30)


CPU_ONLY = False


def ask_model(model, item):
    job = item["job"]
    body = {"model": model, "stream": False, "keep_alive": "10m",
            "messages": [{"role": "system", "content": JOBS[job]["instruction"]},
                         {"role": "user", "content": prompt(item)}],
            "format": schema(job, item.get("choices")),
            "options": {"temperature": 0, "num_ctx": 4096, "num_predict": 96}}
    if CPU_ONLY:
        body["options"].update({"num_gpu": 0, "num_thread": 6})
    if model.startswith("qwen3"):
        body["think"] = False
    t0 = time.time()
    try:
        msg = post("/chat", body)["message"]["content"]
        ans = json.loads(msg).get("answer")
    except (urllib.error.URLError, json.JSONDecodeError, KeyError, TimeoutError, AttributeError) as e:
        return {"id": item["id"], "answer": None, "valid": False, "seconds": time.time() - t0, "error": str(e)[:200]}
    ok = isinstance(ans, str) and (JOBS[job].get("written") or ans in item["choices"])
    return {"id": item["id"], "answer": ans, "valid": bool(ok), "seconds": round(time.time() - t0, 3)}


def unload(model):
    try:
        post("/generate", {"model": model, "keep_alive": 0}, timeout=60)
    except urllib.error.URLError:
        pass


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
    global CPU_ONLY
    CPU_ONLY = "--cpu" in args
    if CPU_ONLY:
        args.remove("--cpu")
    models = args
    items = {j: [json.loads(l) for l in open(f"{DATA}/sets/items-{j}.jsonl")][:limit] for j in jobs}
    total = len(models) * sum(len(v) for v in items.values())
    bar = Task("round1-cpu" if CPU_ONLY else "round1",
               "Round 1: CPU timing" if CPU_ONLY else "Round 1: small models", total=total)
    for model in models:
        out_dir = f"{DATA}/{'timing' if CPU_ONLY else 'runs'}/{model.replace(':', '_').replace('/', '_')}"
        os.makedirs(out_dir, exist_ok=True)
        asked = 0
        for job in jobs:
            path = f"{out_dir}/{job}.jsonl"
            done = set()
            if os.path.exists(path):
                done = {json.loads(l)["id"] for l in open(path)}
            bar.step(len(done & {it['id'] for it in items[job]}))
            with open(path, "a") as f:
                for it in items[job]:
                    if it["id"] in done:
                        continue
                    cool(CPU_ONLY)
                    asked += len(prompt(it))
                    if asked > 60000:  # empties llama-server's in-RAM prompt cache (grows to 8 GB)
                        unload(model)
                        asked = 0
                    f.write(json.dumps(ask_model(model, it)) + "\n")
                    f.flush()
                    bar.step(detail=f"{model} {job}")
        unload(model)
    bar.finish()


if __name__ == "__main__":
    main()
