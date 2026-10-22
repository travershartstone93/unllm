#!/usr/bin/env python3
"""Quick probe of hosted models: the same 10 timeless general questions to
every service, one request per question, answered from memory. Reports right
out of 10 (the clean set's answer patterns), median seconds per answer, and
errors, so services and models can be compared on speed and accuracy before
spending a full run on any of them.

  api_probe.py [provider:model ...]        default: every entry in PROBES

Keys come from the private file named by APIS_ENV (one NAME=value per line;
keep it in the scratchpad). Hosted calls go through curl -4 and OpenAI-style
chat endpoints; "local" uses Ollama's own API with thinking off; "haiku" uses
claude -p. Questions: every 20th item of tests/clean/dev.tsv (10 of them).
Replies must be JSON {"answer": ...}; anything else counts as no answer.
Results: DATA/probe/<provider>_<model>.jsonl and a table on stdout.
"""

import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from evaluate import tsv_items  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import pattern_right  # noqa: E402

N = 10
MAX_TOKENS = 1500   # room for models that think before answering
SYSTEM = ("Answer the question from your own knowledge with the shortest correct answer (a name, date, place or "
          "number). No explanation. Reply with only JSON: {\"answer\": \"...\"}")

# provider -> (chat completions URL, key name, extra body)
PROVIDERS = {
    "groq": ("https://api.groq.com/openai/v1/chat/completions", "GROQ_API_KEY", {}),
    "openrouter": ("https://openrouter.ai/api/v1/chat/completions", "OPENROUTER_API_KEY", {}),
    "cohere": ("https://api.cohere.ai/compatibility/v1/chat/completions", "COHERE_API_KEY", {}),
    "hf": ("https://router.huggingface.co/v1/chat/completions", "HF_TOKEN", {}),
    "ollama": ("https://ollama.com/v1/chat/completions", "OLLAMA_API_KEY", {}),
    "llm7": ("https://api.llm7.io/v1/chat/completions", "LLM7_API_KEY", {}),
    "glm": ("https://api.z.ai/api/paas/v4/chat/completions", "GLM_API_KEY", {"thinking": {"type": "disabled"}}),
}
PROBES = [
    "haiku:haiku", "local:qwen3:4b",
    "groq:openai/gpt-oss-120b", "groq:qwen/qwen3.8-27b",
    "openrouter:nvidia/nemotron-3-ultra-550b-a55b:free", "openrouter:nvidia/nemotron-3-super-120b-a12b:free",
    "cohere:command-a-plus-05-2026", "cohere:command-a-03-2025",
    "ollama:gpt-oss:120b", "ollama:gemma4:31b", "ollama:glm-5.3", "ollama:nemotron-3-super", "ollama:kimi-k2.6",
    "ollama:deepseek-v4.1-flash", "ollama:minimax-m3",
    "llm7:GLM-5.3-Flash", "glm:glm-4.5-flash",
]


def keys():
    out = {}
    for line in open(os.path.expanduser(os.environ["APIS_ENV"])):
        if "=" in line:
            k, v = line.rstrip("\n").split("=", 1)
            out[k] = v
    return out


def parse(text):
    m = re.findall(r"\{[^{}]*\"answer\"[^{}]*\}", text or "", re.S)
    for cand in reversed(m):
        try:
            return str(json.loads(cand).get("answer", "")).strip()
        except ValueError:
            continue
    return None


def curl_json(url, body, headers):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump(body, f)
        path = f.name
    try:
        r = subprocess.run(["curl", "-4", "-s", "-m", "180", "-w", "\n%{http_code}", url, *headers, "-d", "@" + path],
                           capture_output=True, text=True, timeout=190)
    finally:
        os.unlink(path)
    out, _, code = r.stdout.rpartition("\n")
    return out, code


def ask(provider, model, question, api_keys):
    """One question -> (answer or None, seconds, error or None)."""
    t0 = time.time()
    if provider == "haiku":
        r = subprocess.run(["claude", "-p", "--model", "haiku", SYSTEM + "\n\nQuestion: " + question],
                           capture_output=True, text=True, timeout=180, cwd=f"{DATA}/gold")
        return parse(r.stdout), time.time() - t0, None
    if provider == "local":
        body = {"model": model, "stream": False, "think": False, "keep_alive": "5m",
                "messages": [{"role": "system", "content": SYSTEM}, {"role": "user", "content": question}],
                "format": {"type": "object", "properties": {"answer": {"type": "string"}}, "required": ["answer"]},
                "options": {"temperature": 0, "num_predict": 200}}
        out, code = curl_json("http://127.0.0.1:11434/api/chat", body, ["-H", "Content-Type: application/json"])
        if code != "200":
            return None, time.time() - t0, f"HTTP {code}: {out[:120]}"
        return parse(json.loads(out)["message"]["content"]), time.time() - t0, None
    url, key_name, extra = PROVIDERS[provider]
    body = {"model": model, "temperature": 0, "max_tokens": MAX_TOKENS,
            "messages": [{"role": "system", "content": SYSTEM}, {"role": "user", "content": question}], **extra}
    out, code = curl_json(url, body, ["-H", "Content-Type: application/json",
                                      "-H", "Authorization: Bearer " + api_keys[key_name]])
    secs = time.time() - t0
    if code != "200":
        return None, secs, f"HTTP {code}: {out[:120]}"
    try:
        text = json.loads(out)["choices"][0]["message"]["content"] or ""
    except (KeyError, IndexError, ValueError, TypeError):
        return None, secs, "bad reply: " + out[:120]
    return parse(text), secs, None


def main():
    probes = sys.argv[1:] or PROBES
    api_keys = keys()
    dev = [it for it in tsv_items(os.path.expanduser("~/repo-work/unllm/tests/clean/dev.tsv")) if not it.get("unknowable")]
    items = dev[::20][:N]
    os.makedirs(f"{DATA}/probe", exist_ok=True)
    rows = []
    for probe in probes:
        provider, model = probe.split(":", 1)
        results = []
        for it in items:
            ans, secs, err = ask(provider, model, it["question"], api_keys)
            results.append({"id": it["id"], "question": it["question"], "answer": ans, "seconds": round(secs, 2),
                            "error": err, "right": bool(ans and pattern_right(ans, it["pattern"]))})
            if err and "HTTP 40" in err:
                break  # a key or plan problem: no point asking nine more times
        with open(f"{DATA}/probe/{provider}_{model.replace('/', '_').replace(':', '_')}.jsonl", "w") as f:
            for r in results:
                f.write(json.dumps(r) + "\n")
        ok = [r for r in results if not r["error"]]
        answered = sum(1 for r in ok if r["answer"])
        right = sum(r["right"] for r in results)
        med = statistics.median(r["seconds"] for r in ok) if ok else 0
        err = next((r["error"] for r in results if r["error"]), "")
        rows.append((right, probe, answered, med, err))
        print(f"{probe:52} right {right:2}/{N}  answered {answered:2}  median {med:5.1f}s  {err[:70]}", flush=True)
    print("\n| service:model | right of 10 | answered | median s | first error |\n|---|---|---|---|---|")
    for right, probe, answered, med, err in sorted(rows, key=lambda x: (-x[0], x[3])):
        print(f"| {probe} | {right} | {answered} | {med:.1f} | {err[:60]} |")


if __name__ == "__main__":
    main()
