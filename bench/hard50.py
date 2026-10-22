#!/usr/bin/env python3
"""Questions qwen3:4b gets wrong, from English Wikipedia fact boxes, and unllm tested on them.
  hard50.py gen    -> DATA/audit/hard50.tsv (id, question, answer_pattern, answer_type, source_title)
  hard50.py eval   -> four arms, answers in DATA/audit/hard50-eval.jsonl, totals to the audit report"""
import glob, json, os, random, re, sqlite3, subprocess, sys, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
SHARDS = sorted(glob.glob(os.path.expanduser("~/.local/share/unllm/packs/wikipedia-en/shard-*.sqlite")))
TEMPLATES = {"Born": ("In what year was {t} born?", "year"), "Founded": ("In what year was {t} founded?", "year"),
             "Established": ("In what year was {t} established?", "year"), "Released": ("In what year was {t} released?", "year"),
             "Directed by": ("Who directed {t}?", "person")}


def ollama(prompt):
    body = json.dumps({"model": "qwen3:4b", "messages": [{"role": "user", "content": prompt}], "stream": False, "format": "json",
                       "think": False, "options": {"temperature": 0, "num_ctx": 8192}}).encode()
    req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            o = json.loads(json.loads(r.read())["message"]["content"])
        return str(o.get("answer", "")).strip(), int(o.get("passage") or 0)
    except (ValueError, OSError, KeyError, TypeError):
        return "", 0


MEMORY = 'Answer from your own knowledge with the shortest exact answer (a name or a year). Never say you don\'t know.\n\nQuestion: {q}\n\nReply with only JSON: {{"answer": "..."}}'


def gen():
    global ALL
    ALL = [sqlite3.connect(f"file:{p}?mode=ro", uri=True) for p in SHARDS]
    random.seed(19)
    cands = []
    for path in SHARDS:
        con = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
        rows = con.execute("SELECT d.doc_id, d.title, i.key, i.value FROM infobox i JOIN docs d ON d.doc_id = i.doc_id "
                           "WHERE i.key IN ('Born','Founded','Established','Released','Directed by') AND d.title NOT LIKE '%(%' "
                           "ORDER BY random() LIMIT 400").fetchall()
        for doc_id, title, key, value in rows:
            if TEMPLATES[key][1] == "year":
                m = re.search(r"\b(1[5-9]\d\d|20[0-2]\d)\b", value)
                if not m:
                    continue
                ans, pat = m.group(1), rf"\b{m.group(1)}\b"
            else:
                name = re.split(r"\s*[·,;]\s*|\s+and\s+", value)[0].strip()
                if len(name.split()) < 2 or not re.match(r"^[A-Z][\w.'\- ]+$", name):
                    continue
                ans, pat = name, r"\b(?:" + re.escape(name) + "|" + re.escape(name.split()[-1]) + r")\b"
            text = " ".join(t for (t,) in con.execute("SELECT text FROM passages WHERE doc_id = ? AND role IN ('lead','body')", (doc_id,)))
            if not re.search(pat, text, re.I):
                continue
            tk = con.execute("SELECT title_key FROM docs WHERE doc_id = ?", (doc_id,)).fetchone()[0]
            n_same = sum(c.execute("SELECT count(*) FROM docs WHERE title_key = ?", (tk,)).fetchone()[0] for c in ALL)
            if n_same != 1:
                continue
            cands.append((TEMPLATES[key][0].format(t=title), pat, TEMPLATES[key][1], title, ans))
    random.shuffle(cands)
    bar = Task("hard50gen", "Finding questions the 4B gets wrong", total=50)
    keep, asked = [], 0
    for q, pat, typ, title, ans in cands:
        if len(keep) == 50:
            break
        got, _ = ollama(MEMORY.format(q=q))
        asked += 1
        if not (got and re.search(pat, got, re.I)):
            keep.append((f"h{len(keep) + 1:03d}", q, pat, typ, title, ans, got))
            bar.step()
    bar.finish()
    with open(f"{A}/hard50.tsv", "w") as f:
        f.write("id\tquestion\tanswer_pattern\tanswer_type\tsource_title\n")
        f.writelines(f"{i}\t{q}\t{p}\t{t}\t{s}\n" for i, q, p, t, s, _, _ in keep)
    print(f"candidates {len(cands)}, asked {asked}, the 4B got {asked - len(keep)} right, kept {len(keep)} it got wrong")
    for i, q, p, t, s, a, g in keep:
        print(f"  {i} {q[:70]} | key {a} | 4B said: {g[:30]}")


PASSAGES = """Answer the question using ONLY the numbered passages below; do not use your own knowledge.

Question: {q}

Passages:
{ps}

Give the shortest exact answer as it is stated in a passage, and that passage's number. If no passage states the answer, reply with answer "NONE" and passage 0.
Reply with only JSON: {{"answer": "...", "passage": N}}"""
FALLBACK = """Answer the question. Encyclopedia passages found by a search are below: if one states the answer, take it from there and give its number; otherwise answer from your own knowledge and give 0. Never say you don't know.

Question: {q}

Passages:
{ps}

Reply with only JSON: {{"answer": "...", "passage": N}}"""


def evaluate():
    import rerank
    from qa_score import run_unllm
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    ranker = rerank.load("qwen3-0.6b")
    rows = [l.split("\t") for l in open(f"{A}/hard50.tsv").read().splitlines()[1:]]
    hit = lambda pat, t: bool(t) and t != "NONE" and re.search(pat, t, re.I) is not None  # noqa: E731
    out, bar = [], Task("hard50eval", "unllm on questions the 4B gets wrong", total=len(rows))
    for i, q, pat, typ, src in rows:
        rep = run_unllm(CLI, q, passages=10)
        ps = [" ".join(p.split())[:900] for p in rep["passages"][:10]]
        sc = ranker.scores(q, ps) if ps else []
        rr = [ps[k] for k in sorted(range(len(ps)), key=lambda k: -float(sc[k]))]
        fmt = "\n\n".join(f"[{n}] {p}" for n, p in enumerate(rr[:3], 1))
        mem, _ = ollama(MEMORY.format(q=q))
        only, _ = ollama(PASSAGES.format(q=q, ps=fmt)) if rr else ("NONE", 0)
        fb, _ = ollama(FALLBACK.format(q=q, ps=fmt)) if rr else (mem, 0)
        out.append({"id": i, "question": q, "pattern": pat, "in_top10": any(hit(pat, p) for p in ps),
                    "in_reranked_top3": any(hit(pat, p) for p in rr[:3]), "4b_alone": mem, "4b_alone_right": hit(pat, mem),
                    "unllm": rep["answer"][:300], "unllm_right": hit(pat, rep["answer"]),
                    "rr_4b_only": only, "rr_4b_only_right": hit(pat, only), "rr_4b_fallback": fb, "rr_4b_fallback_right": hit(pat, fb)})
        bar.step(detail=i)
    bar.finish()
    open(f"{A}/hard50-eval.jsonl", "w").write("".join(json.dumps(r) + "\n" for r in out))
    s = lambda k: sum(r[k] for r in out)  # noqa: E731
    lines = ["\n### Questions qwen3:4b gets wrong (hard50, from English Wikipedia fact boxes), 2026-10-19/20",
             f"- 4B alone (rerun): {s('4b_alone_right')} of 50",
             f"- unllm current defaults: {s('unllm_right')} of 50",
             f"- unllm search -> reranker -> 4B on the top 3, passages only: {s('rr_4b_only_right')} of 50",
             f"- same, 4B may fall back on its own knowledge: {s('rr_4b_fallback_right')} of 50",
             f"- answer anywhere in unllm's top 10 passages: {s('in_top10')}; in the reranked top 3: {s('in_reranked_top3')}"]
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    {"gen": gen, "eval": evaluate}[sys.argv[1]]()
