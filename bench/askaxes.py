#!/usr/bin/env python3
"""ask, the three axes of a kind measured apart on a labelled set, then composed.
  topic:  the nearest phrasing's topic (ask --kind)
  window: unllm --read (live -> now; a stated time -> window; nothing stated -> the default, counted as
          a miss unless the label says either); the "tense" variant reads an unstated time from the verb
  form:   the analyzer's wants / form / judge mapped to the catalogue's answer forms
The label is the kind id's topic, window and form from kinds.tsv. Composed: all three right, and whether
a kind with that topic, window and form exists in the catalogue.
  askaxes.py LABEL SET.json [SET.json ...]"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
ASK = os.path.expanduser("~/repo-work/ActivityScope/build/ask")
UNLLM = os.path.expanduser("~/repo-work/unllm/build/unllm")
KINDS = os.path.expanduser("~/repo-work/ActivityScope/data/kinds/kinds.tsv")
merges = json.load(open(f"{A}/ask-catalogue-v3-merges.json"))
TO_V4 = {mid: m["keep"] for m in merges for mid in m.get("merge", [])}
# Past tense, for the prototype only: the irregular forms the records' questions use, and regular -ed
# forms the lemmatizer changed. In the analyzer this belongs in the lemma table as a tense mark.
PAST = {"was", "were", "did", "had", "got", "went", "ran", "froze", "died", "happened", "crashed", "used", "ate", "took",
        "came", "made", "started", "stopped", "kept", "began", "lost", "hit", "spiked", "ran", "woke", "slept"}


def catalogue():
    kinds = {}
    for line in open(KINDS).read().splitlines()[1:]:
        f = line.split("\t")
        kinds[f[0]] = {"topic": f[1], "form": f[2], "window": f[3]}
    return kinds


def read(q):
    out = subprocess.run([UNLLM, "--read", q], capture_output=True, text=True, timeout=60).stdout
    return json.loads(out) if out.startswith("{") else {}


def form_of(r):
    """The catalogue's answer form from the analyzer's reading."""
    if r.get("judge", "none") != "none": return "a comparison or trend"
    w = r.get("wants", "")
    if w == "cause": return "a cause"
    if w == "time": return "when"
    if w in ("duration", "count"): return "how often or how long"
    if r.get("form") == "polar" or w == "yesno": return "yes or no"
    if w == "name": return "which thing"
    return "a value"  # quantity or fact: a figure, now or over the window


def main(label, sets):
    kinds = catalogue()
    lines = [f"\n### ask, the axes measured apart: {label} ({time.strftime('%Y-%m-%d %H:%M')})"]
    bar = Task("askaxes", f"ask axes: {label}", total=sum(len(json.load(open(f'{A}/{s}'))) for s in sets))
    for name in sets:
        rows = json.load(open(f"{A}/{name}"))
        qs = [r["question"] for r in rows]
        got = [json.loads(l) for l in subprocess.run([ASK, "--kind"], input="\n".join(qs) + "\n", capture_output=True, text=True, timeout=1800).stdout.splitlines() if l.startswith("{")]
        n = {"topic": 0, "window": 0, "window+tense": 0, "form": 0, "composed": 0, "composed+tense": 0, "exists": 0, "nearest kind": 0}
        rec, total = [], 0
        for r, g in zip(rows, got):
            lab = TO_V4.get(r.get("kind") or r.get("opus"), r.get("kind") or r.get("opus"))
            if lab == "none" or lab not in kinds:
                continue
            total += 1
            L = kinds[lab]
            lf = L["form"] if L["form"] != "a current value" and L["form"] != "a value over a window" else "a value"
            rd = read(r["question"])
            topic = g["kind"].split(".")[0]
            win = "now" if rd.get("live") else ("window" if rd.get("stated") else "")
            toks = [t.split("/")[0] for t in rd.get("tokens", [])]
            tense = win or ("window" if any(t in PAST or (t.endswith("ed") and len(t) > 4) for t in toks) else "now")
            form = form_of(rd)
            ok_t = topic == L["topic"]
            ok_w = L["window"] == "either" or win == L["window"]
            ok_wt = L["window"] == "either" or tense == L["window"]
            ok_f = form == lf or (lf == "identity" and L["topic"] == "identity" and topic == "identity") or (lf == "a list" and form == "which thing")
            n["topic"] += ok_t; n["window"] += ok_w; n["window+tense"] += ok_wt; n["form"] += ok_f
            n["composed"] += ok_t and ok_w and ok_f; n["composed+tense"] += ok_t and ok_wt and ok_f
            n["nearest kind"] += g["kind"] == lab
            # Does a kind with the composed topic, window and form exist?
            cand = [k for k, v in kinds.items() if v["topic"] == topic and (v["window"] in (tense, "either")) and
                    ((v["form"] if v["form"] not in ("a current value", "a value over a window") else "a value") == form or (form == "which thing" and v["form"] == "a list"))]
            n["exists"] += bool(cand)
            # Chosen by the catalogue: the kinds of this topic and window; the analyzer decides where it has a
            # clear say (why, when, how often, compared to normal) and such a kind exists; the nearest phrasing
            # among the rest otherwise; the flat nearest kind when the topic and window fit nothing.
            score = dict(g["runners"])
            fits = [k for k, v in kinds.items() if v["topic"] == topic and v["window"] in (tense, "either")]
            strong = {"a cause": "a cause", "when": "when", "how often or how long": "how often or how long", "a comparison or trend": "a comparison or trend"}
            chosen = ""
            if form in strong:
                named = [k for k in fits if kinds[k]["form"] == strong[form]]
                if named: chosen = max(named, key=lambda k: score.get(k, -2))
            if not chosen and fits: chosen = max(fits, key=lambda k: score.get(k, -2))
            if not chosen: chosen = g["kind"]
            n["chosen"] = n.get("chosen", 0) + (chosen == lab)
            by_score = max(fits, key=lambda k: score.get(k, -2)) if fits else g["kind"]
            n["chosen, no analyzer"] = n.get("chosen, no analyzer", 0) + (by_score == lab)
            rec.append({"question": r["question"], "label": lab, "topic": topic, "window": win, "tense": tense, "form": form, "label_form": lf,
                        "label_window": L["window"], "ok": [ok_t, ok_wt, ok_f], "nearest": g["kind"], "composed_exists": cand[:3], "chosen": chosen})
            bar.step(detail=name)
        with open(f"{A}/askaxes-{label}-{name.replace('.json', '')}.jsonl", "w") as f:
            f.writelines(json.dumps(x) + "\n" for x in rec)
        lines.append(f"- {name} ({total} answerable): topic {n['topic']}, window as parsed {n['window']}, window with tense {n['window+tense']}, "
                     f"form {n['form']}; composed {n['composed']}, composed with tense {n['composed+tense']} (a kind exists for the composition "
                     f"on {n['exists']}); nearest kind alone {n['nearest kind']}; chosen by catalogue {n.get('chosen', 0)}, without the analyzer {n.get('chosen, no analyzer', 0)}")
    bar.finish()
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
