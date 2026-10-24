#!/usr/bin/env python3
"""The kind matcher inside ask (ask --kind) on a labelled set: exact kind and topic.
  askkind_cli.py LABEL SET.json [SET.json ...]   sets are lists of {"question", "kind"} (or "opus" as the label)"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
ASK = os.path.expanduser("~/repo-work/ActivityScope/build/ask")
merges = json.load(open(f"{A}/ask-catalogue-v3-merges.json"))
TO_V3 = {mid: m["keep"] for m in merges for mid in m.get("merge", [])}


def main(label, sets):
    lines = [f"\n### ask --kind on labelled sets: {label} ({time.strftime('%Y-%m-%d %H:%M')})"]
    bar = Task("askkindcli", f"ask kinds in C++: {label}", total=len(sets))
    for name in sets:
        rows = json.load(open(f"{A}/{name}"))
        qs = [r["question"] for r in rows]
        out = subprocess.run([ASK, "--kind"], input="\n".join(qs) + "\n", capture_output=True, text=True, timeout=1800).stdout.splitlines()
        got = [json.loads(l) for l in out if l.startswith("{")]
        exact = topic = 0
        rec = []
        for r, g in zip(rows, got):
            lab = TO_V3.get(r.get("kind") or r.get("opus"), r.get("kind") or r.get("opus"))
            ok = g["kind"] == lab
            exact += ok
            topic += bool(g["kind"]) and lab != "none" and g["kind"].split(".")[0] == lab.split(".")[0]
            rec.append({"question": r["question"], "label": lab, "got": g["kind"], "score": g["score"], "via": g["via"], "right": ok})
        with open(f"{A}/askkind-cli-{label}-{name.replace('.json', '')}.jsonl", "w") as f:
            f.writelines(json.dumps(x) + "\n" for x in rec)
        lines.append(f"- {name}: exact kind {exact} of {len(rows)}, topic {topic} of {len(rows)}")
        bar.step(detail=name)
    bar.finish()
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
