#!/usr/bin/env python3
"""The follow-up rewrite (unllm --rewrite, the reader model) on 50 QReCC test turns: 40 that depend
on their conversation (Question != Rewrite) and 10 that stand alone. The previous turn's gold rewrite
and answer play the previous exchange. Haiku judges whether unllm's rewrite means the same as the
gold rewrite. Rows -> DATA/audit/rewrite50.jsonl, totals to the audit report."""
import json, os, random, re, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
GOLD = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/gold")
QRECC = os.path.expanduser("~/Downloads/unllm-benchmarks/qrecc/data/qrecc_test.json")
JUDGE = """For each item, say whether the candidate question asks the same thing as the reference question (same subject, same request; wording may differ). A candidate that still leaves out what the reference names is not the same.

Items:
{items}

Reply with only a JSON list: [{{"item": N, "same": true or false}}, ...]"""


def main():
    data = json.load(open(QRECC))
    by_conv = {}
    for t in data:
        by_conv.setdefault(t["Conversation_no"], []).append(t)
    rng = random.Random(11)
    dep, alone = [], []
    for conv in by_conv.values():
        conv.sort(key=lambda t: t["Turn_no"])
        for i in range(1, len(conv)):
            prev, cur = conv[i - 1], conv[i]
            if not prev["Answer"] or not cur["Rewrite"] or not cur["Question"]:
                continue
            (alone if cur["Question"].strip() == cur["Rewrite"].strip() else dep).append((prev, cur))
    rng.shuffle(dep)
    rng.shuffle(alone)
    items = [dict(kind="depends", prev=p, cur=c) for p, c in dep[:40]] + [dict(kind="alone", prev=p, cur=c) for p, c in alone[:10]]
    bar = Task("rewrite50", "Follow-up rewrite, 50 QReCC turns", total=len(items))
    for it in items:
        stdin = f"{it['prev']['Rewrite']}\n{' '.join(it['prev']['Answer'].split())[:600]}\n{it['cur']['Question']}\n"
        out = subprocess.run([os.path.expanduser("~/repo-work/unllm/build/unllm"), "--rewrite"], input=stdin,
                             capture_output=True, text=True, timeout=180).stdout
        m = re.search(r'"question": "(.*)"\}', out, re.S)
        it["got"] = m.group(1) if m else ""
        bar.step(detail=it["kind"])
    bar.finish()
    prompt = JUDGE.format(items="\n\n".join(f"[{i}] reference: {it['cur']['Rewrite']}\ncandidate: {it['got']}" for i, it in enumerate(items, 1)))
    r = subprocess.run(["claude", "-p", "--model", "claude-haiku-5-5", prompt], capture_output=True, text=True, timeout=900,
                       stdin=subprocess.DEVNULL, cwd=GOLD).stdout
    m = re.search(r"\[.*\]", r, re.S)
    verdict = {int(v["item"]): bool(v["same"]) for v in (json.loads(m.group(0)) if m else []) if isinstance(v, dict)}
    for i, it in enumerate(items, 1):
        it["same"] = verdict.get(i, False)
        it["unchanged"] = it["got"].strip() == it["cur"]["Question"].strip()
    with open(f"{A}/rewrite50.jsonl", "w") as f:
        for it in items:
            f.write(json.dumps({"kind": it["kind"], "prev_q": it["prev"]["Rewrite"], "question": it["cur"]["Question"],
                                "gold": it["cur"]["Rewrite"], "got": it["got"], "same": it["same"], "unchanged": it["unchanged"]}) + "\n")
    dep_ok = sum(it["same"] for it in items if it["kind"] == "depends")
    alone_ok = sum(it["same"] for it in items if it["kind"] == "alone")
    alone_unch = sum(it["unchanged"] for it in items if it["kind"] == "alone")
    lines = [f"\n### Follow-up rewrite by qwen3:4b on 50 QReCC turns ({time.strftime('%Y-%m-%d %H:%M')})",
             f"- depends on the conversation: {dep_ok} of 40 rewrites mean the same as the gold rewrite (Haiku judged)",
             f"- stands alone: {alone_ok} of 10 judged the same, {alone_unch} of 10 returned word for word unchanged",
             f"- total: {dep_ok + alone_ok} of 50"]
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
