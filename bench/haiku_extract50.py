#!/usr/bin/env python3
"""Haiku in the answer-extraction stage, verified dev 50 (DATA/audit/dev50.tsv). One library run
per question with production defaults gives the baseline answer and the top 10 passages; Haiku
answers from those passages only. Scored with the verified patterns."""
import json, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from qa_score import run_unllm  # noqa: E402
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
PROMPT = """Do not use any tools. Answer the question using ONLY the numbered passages below; do not use your own knowledge.

Question: {q}

Passages:
{ps}

Give the shortest exact answer as it is stated in a passage, and that passage's number. If no passage states the answer, reply with answer "NONE" and passage 0.
Reply with only JSON: {{"answer": "...", "passage": N}}"""


def haiku(prompt):
    r = subprocess.run(["claude", "-p", "--model", "haiku", prompt], capture_output=True, text=True, timeout=300,
                       stdin=subprocess.DEVNULL, cwd=os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/gold"))
    t = r.stdout
    at = t.rfind('{"answer"')
    try:
        o = json.loads(t[at:t.index("}", at) + 1]) if at >= 0 else {}
    except ValueError:
        o = {}
    return str(o.get("answer", "")).strip(), int(o.get("passage") or 0)


def main():
    rows = [dict(zip(("id", "question", "pattern", "type", "source"), l.split("\t")))
            for l in open(f"{A}/dev50.tsv").read().splitlines()[1:]]
    env_clean = {k: v for k, v in os.environ.items() if not k.startswith("UNLLM")}
    os.environ.clear()
    os.environ.update(env_clean)
    bar = Task("haiku50", "Haiku answer step, 50 questions", total=len(rows))
    out, base_n, h_n, h_cited = [], 0, 0, 0
    for q in rows:
        rep = run_unllm(CLI, q["question"], passages=10)
        ps = [" ".join(p.split())[:900] for p in rep["passages"][:10]]
        ans, k = haiku(PROMPT.format(q=q["question"], ps="\n\n".join(f"[{i}] {p}" for i, p in enumerate(ps, 1)))) if ps else ("NONE", 0)
        b = re.search(q["pattern"], rep["answer"], re.I) is not None
        h = ans != "NONE" and re.search(q["pattern"], ans, re.I) is not None
        hc = h and 0 < k <= len(ps) and re.search(q["pattern"], ps[k - 1], re.I) is not None
        base_n += b; h_n += h; h_cited += hc
        out.append({"id": q["id"], "question": q["question"], "unllm": rep["answer"][:300], "unllm_right": b,
                    "haiku": ans, "cited": k, "haiku_right": h, "haiku_cited_ok": hc, "n_passages": len(ps)})
        bar.step()
    bar.finish()
    with open(f"{A}/haiku-extract50.jsonl", "w") as f:
        f.write("".join(json.dumps(r) + "\n" for r in out))
    g = [r for r in out if r["haiku_right"] and not r["unllm_right"]]
    l = [r for r in out if r["unllm_right"] and not r["haiku_right"]]
    lines = [f"\n### Haiku as the answer step, verified dev 50, same passages (2026-10-19, night)",
             f"- unllm's own answer step: {base_n} of 50; Haiku from the same top 10 passages: {h_n} of 50 "
             f"({h_cited} with the cited passage holding the answer); paired: Haiku gained {len(g)}, lost {len(l)}"]
    print("\n".join(lines))
    for tag, rs in (("GAINED", g), ("LOST", l)):
        for r in rs:
            print(f"{tag} {r['id']} {r['question'][:45]} | unllm: {' '.join(r['unllm'].split())[:70]} | haiku: {r['haiku'][:40]} (p{r['cited']})")
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
