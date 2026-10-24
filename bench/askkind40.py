#!/usr/bin/env python3
"""ask: which matcher maps a question to its catalogue kind best, on machine40 with Opus's kind labels.
Method 2, "4b": qwen3:4b picks the kind id from the whole catalogue (JSON reply).
Method 3, "embed": bge-small embeds every catalogue phrasing once; the question goes to the kind of the
nearest phrasing ("embed1"), and to the kind most of the 5 nearest phrasings belong to ("embed5").
Rows -> DATA/audit/askkind40.jsonl, totals to the audit report."""
import json, os, sys, time, urllib.request
from collections import Counter
import numpy as np, torch
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
HF = os.path.expanduser("~/Downloads/unllm-models/hf/bge-small-en-v1.5")
SET = os.environ.get("ASKKIND_SET", "machine40-kinds.json")
TAG = os.environ.get("ASKKIND_TAG", "machine40")
CAT = os.environ.get("ASKKIND_CAT", "ask-catalogue.json")          # the phrasing bank to match against
WINDOWS = os.environ.get("ASKKIND_WINDOWS")                         # question -> "now" | "window" from unllm --read
SKIP_4B = os.environ.get("ASKKIND_NO_4B") == "1"


def topic(kind):
    return kind.split(".")[0]


def ollama_pick(question, kinds_text):
    prompt = ("Below is the catalogue of question kinds a deterministic laptop assistant can answer. Pick the ONE kind "
              "that answers the user's question, judging by what computation would answer it. Reply with only JSON: "
              "{\"kind\": \"id\"}\n\nKinds:\n" + kinds_text + "\n\nQuestion: " + question)
    body = json.dumps({"model": "qwen3:4b", "messages": [{"role": "user", "content": prompt}], "stream": False, "format": "json",
                       "think": False, "options": {"temperature": 0, "num_ctx": 16384}}).encode()
    req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            return str(json.loads(json.loads(r.read())["message"]["content"]).get("kind", "")).strip()
    except (ValueError, OSError, KeyError, TypeError):
        return ""


def main():
    cat = json.load(open(f"{A}/{CAT}"))
    wins = json.load(open(f"{A}/{WINDOWS}")) if WINDOWS else {}
    kind_window = {k["id"]: k["window"] for k in cat}
    ids = {k["id"] for k in cat}
    rows = json.load(open(f"{A}/{SET}"))
    kinds_text = "\n".join(f"{k['id']}: {k['answer'][:110]} (e.g. \"{k['phrasings'][0]}\")" for k in cat)
    from transformers import AutoModel, AutoTokenizer
    tok = AutoTokenizer.from_pretrained(HF)
    model = AutoModel.from_pretrained(HF).eval()

    @torch.no_grad()
    def embed(texts):
        out = []
        for i in range(0, len(texts), 64):
            e = tok(texts[i:i + 64], padding=True, truncation=True, max_length=64, return_tensors="pt")
            h = model(**e).last_hidden_state[:, 0]
            out.append(torch.nn.functional.normalize(h, dim=1))
        return torch.cat(out).numpy()
    phr = [(k["id"], p) for k in cat for p in k["phrasings"]]
    P = embed([p for _, p in phr])
    bar = Task("askkind40", f"ask kinds: 4B vs embedding, {TAG}", total=len(rows))
    out, tot = [], Counter()
    for r in rows:
        q = embed([r["question"]])[0]
        sims = P @ q
        # Experiment 2: the deterministic time window keeps only the kinds of that window ("either" always stays).
        w = wins.get(r["question"])
        if w:
            sims = np.where(np.array([kind_window[k] in (w, "either") for k, _ in phr]), sims, -1.0)
        order = np.argsort(-sims)
        e1 = phr[order[0]][0]
        e5 = Counter(phr[i][0] for i in order[:5]).most_common(1)[0][0]
        b = "" if SKIP_4B else ollama_pick(r["question"], kinds_text)
        if b not in ids:
            b = ""
        rec = {"question": r["question"], "label": r["kind"], "4b": b, "embed1": e1, "embed5": e5, "nearest": phr[order[0]][1],
               "sim": float(sims[order[0]]), "window": w or ""}
        for m in ("4b", "embed1", "embed5"):
            tot[m + " exact"] += rec[m] == r["kind"]
            tot[m + " topic"] += bool(rec[m]) and topic(rec[m]) == topic(r["kind"])
        out.append(rec)
        bar.step(detail=r["question"][:30])
    bar.finish()
    with open(f"{A}/askkind-{TAG}.jsonl", "w") as f:
        f.writelines(json.dumps(r) + "\n" for r in out)
    n = len(rows)
    lines = [f"\n### ask kinds on {TAG}: bank {CAT} ({len(phr)} phrasings){', window-restricted' if wins else ''} ({time.strftime('%Y-%m-%d %H:%M')})"]
    for m, name in (("4b", "qwen3:4b picks from the catalogue"), ("embed1", "nearest phrasing"), ("embed5", "vote of 5 nearest")):
        lines.append(f"- {name}: exact kind {tot[m + ' exact']} of {n}, topic {tot[m + ' topic']} of {n}")
    # Experiment 3: where the nearest similarity sits for questions with no kind versus the rest.
    none_s = sorted(round(r["sim"], 3) for r in out if r["label"] == "none")
    real_s = sorted(round(r["sim"], 3) for r in out if r["label"] != "none")
    if none_s:
        lines.append(f"- nearest similarity, no-kind questions: {none_s}; real questions: lowest 5 {real_s[:5]}, median {real_s[len(real_s) // 2]}")
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
