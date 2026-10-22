#!/usr/bin/env python3
"""Reader layer for ask: qwen3:4b vs ask's current word-list reader, on 40 hand-labelled machine
questions (DATA/audit/machine40.tsv: topic from ask's own topic list, time now or past)."""
import json, os, subprocess, sys, urllib.request
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402
A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
ASK = os.path.expanduser("~/repo-work/ActivityScope/build/ask")
TOPICS = {"heat": "CPU temperature, fan noise, overheating", "slow": "CPU load, what programs are running or using the CPU, slowness",
          "memory": "RAM and swap: use, free, what uses memory", "disk": "disk space and disk activity",
          "network": "network traffic and connections", "gpu": "the graphics card: load, temperature, video memory",
          "battery": "battery charge, charger, power draw", "crash": "crashes, freezes, unexpected shutdowns",
          "update": "software installed, updated or removed", "config": "settings changes", "startup": "programs started at boot",
          "devices": "USB and other hardware plugged in or removed", "sharing": "torrents and file sharing",
          "trust": "whether programs are safe", "identity": "what the machine is: hardware, kernel, OS, uptime, hostname",
          "diagnose": "what is wrong with the machine in general"}
PROMPT = ("Classify a question about this computer.\nTopics:\n" + "\n".join(f"- {k}: {v}" for k, v in TOPICS.items()) +
          "\nTime: \"now\" if it asks about the present state or fixed facts, \"past\" if it asks about an earlier time or a period."
          "\n\nQuestion: {q}\n\nReply with only JSON: {{\"topic\": \"...\", \"time\": \"now\" or \"past\"}}")


def four_b(q):
    body = json.dumps({"model": "qwen3:4b", "messages": [{"role": "user", "content": PROMPT.replace("{q}", q)}], "stream": False,
                       "format": "json", "think": False, "options": {"temperature": 0}}).encode()
    req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=300) as r:
        o = json.loads(json.loads(r.read())["message"]["content"])
    return str(o.get("topic", "")).lower().strip(), str(o.get("time", "")).lower().strip()


def current(q):
    out = subprocess.run([ASK, "--branch", q], capture_output=True, text=True, timeout=120).stdout
    br = next((l[9:-1] for l in out.splitlines() if l.startswith("[branch:")), "-")
    head, _, tail = br.partition(":")
    if head == "live": return tail, "now"
    if head in ("mask", "most"): return tail, "past"
    if head == "identity": return "identity", "now"
    if head == "log" and tail == "problems": return "crash", "past"
    return f"({br})", "-"


rows = [l.split("\t") for l in open(f"{A}/machine40.tsv").read().splitlines()[1:]]
bar = Task("askreader40", "4B reader for ask, 40 questions", total=len(rows))
score = {"current": [0, 0, 0], "4B": [0, 0, 0]}
lines = []
for q, topic, time in rows:
    res = {"current": current(q), "4B": four_b(q)}
    for k, (t, w) in res.items():
        score[k][0] += t == topic; score[k][1] += w == time; score[k][2] += t == topic and w == time
    lines.append(f"{q[:48]:48s} label {topic}/{time:4s} | current {res['current'][0]}/{res['current'][1]} | 4B {res['4B'][0]}/{res['4B'][1]}")
    bar.step()
bar.finish()
for k, (t, w, both) in score.items():
    print(f"{k:8s} topic right {t} of 40, time right {w} of 40, both right {both} of 40")
print("\n".join(lines))
open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a").write(
    "\n### Reader layer for ask, 40 hand-labelled machine questions (2026-10-20)\n" +
    "".join(f"- {k}: topic {t}/40, time {w}/40, both {b}/40\n" for k, (t, w, b) in score.items()))
