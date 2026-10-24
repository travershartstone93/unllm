#!/usr/bin/env python3
"""ask end to end: topic and window from the branch label (ask --branch) against the labelled sets.
  live:TOPIC -> (topic, now); mask:/most:/story TOPIC -> (topic, window); identity -> identity; anything
  else (clarify, refusal, banter, what-is) -> no topic. Label window "either" accepts any.
  askend.py LABEL SET.json [SET.json ...]"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
ASK = os.environ.get("ASK_CLI") or os.path.expanduser("~/repo-work/ActivityScope/build/ask")
KINDS = os.path.expanduser("~/repo-work/ActivityScope/data/kinds/kinds.tsv")
merges = json.load(open(f"{A}/ask-catalogue-v3-merges.json"))
TO_V4 = {mid: m["keep"] for m in merges for mid in m.get("merge", [])}
NAMES = {"update": "updates", "crash": "crashes", "problems": "crashes", "software": "updates", "session": "battery", "flashing": "flash"}


def main(label, sets):
    kinds = {l.split("\t")[0]: l.split("\t") for l in open(KINDS).read().splitlines()[1:]}
    lines = [f"\n### ask end to end, topic and window from the branch: {label} ({time.strftime('%Y-%m-%d %H:%M')})"]
    bar = Task("askend", f"ask end to end: {label}", total=sum(len(json.load(open(f'{A}/{s}'))) for s in sets))
    for name in sets:
        rows = json.load(open(f"{A}/{name}"))
        n = {"answerable": 0, "topic": 0, "topic+window": 0}
        rec = []
        for r in rows:
            lab = TO_V4.get(r.get("kind") or r.get("opus"), r.get("kind") or r.get("opus"))
            bar.step(detail=name)
            if lab not in kinds and lab != "none":
                continue
            if lab == "none" or kinds[lab][4] == "missing":  # unanswerable: right when ask says so
                out = subprocess.run([ASK, "--branch", r["question"]], capture_output=True, text=True, errors="replace", timeout=300).stdout
                branch = next((l[9:-1] for l in out.splitlines() if l.startswith("[branch: ")), "")
                n["unanswerable"] = n.get("unanswerable", 0) + 1
                n["honest"] = n.get("honest", 0) + branch.startswith("unanswered:")
                rec.append({"question": r["question"], "label": lab, "branch": branch, "honest": branch.startswith("unanswered:"),
                            "answer": " ".join(out.split("\n\n")[0].split())[:200]})
                continue
            n["answerable"] += 1
            ltopic, lwin = kinds[lab][1], kinds[lab][3]
            out = subprocess.run([ASK, "--branch", r["question"]], capture_output=True, text=True, errors="replace", timeout=300).stdout
            branch = next((l[9:-1] for l in out.splitlines() if l.startswith("[branch: ")), "")
            tline = next((l[8:-1] for l in out.splitlines() if l.startswith("[topic: ")), "")
            head, _, tail = branch.partition(":")
            topic = NAMES.get(tail, tail) if head in ("live", "mask", "most", "story", "log") else ("identity" if head == "identity" else "")
            win = "now" if head == "live" else ("window" if head in ("mask", "most", "story", "log") else "")
            if tline:  # the reading itself, when the CLI prints it: topic and when
                tp, _, tw = tline.partition(" ")
                topic, win = NAMES.get(tp, tp), tw
            if topic == "identity": win = lwin  # identity answers have no window
            ok_t = topic == ltopic
            ok_w = lwin == "either" or win == lwin
            n["refused"] = n.get("refused", 0) + branch.startswith("unanswered:")
            n["topic"] += ok_t
            n["topic+window"] += ok_t and ok_w
            rec.append({"question": r["question"], "label": lab, "branch": branch, "topic": topic, "window": win, "ok": [ok_t, ok_t and ok_w],
                        "answer": " ".join(out.split("\n\n")[0].split())[:200]})
        with open(f"{A}/askend-{label}-{name.replace('.json', '')}.jsonl", "w") as f:
            f.writelines(json.dumps(x) + "\n" for x in rec)
        lines.append(f"- {name}: of {n['answerable']} answerable, topic right {n['topic']}, topic and window right {n['topic+window']}, "
                     f"refused {n.get('refused', 0)}; of {n.get('unanswerable', 0)} unanswerable, honest reply {n.get('honest', 0)}")
    bar.finish()
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
