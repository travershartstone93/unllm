#!/usr/bin/env python3
"""Questions from every pack that qwen3:4b gets wrong, and unllm tested on them.
  corpus50.py gen [SEED] [NAME]   (default 31, corpus50) sample passages from every pack; Haiku writes one question per passage with an
                     answer key stated in it; Opus 5.5 throws out ambiguous questions and wrong keys;
                     qwen3:4b answers from memory (Haiku judges wording); keep 50 the 4B gets wrong,
                     spread across packs -> DATA/audit/corpus50.tsv (+ corpus50-gen.jsonl, every step)
  corpus50.py eval   vector search over the full index + Qwen3-Reranker-0.6B + unllm's plain quote
                     -> DATA/audit/corpus50-eval.jsonl, totals to the audit report
  corpus50.py eval4b same search and reranker, qwen3:4b reads the reranked top 3 (passages only, the
                     rerank_4b50.py prompt) -> DATA/audit/corpus50-eval4b.jsonl
  corpus50.py reread MODEL   another Ollama model (or "groq": qwen/qwen3.8-27b, key from APIS_ENV) reads the exact top 3 the eval4b run saved (no search,
                     no reranker: a reader-only comparison) -> DATA/audit/corpus50-reread-MODEL.jsonl"""
import glob, json, os, random, re, sqlite3, subprocess, sys, urllib.request
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
from taskprogress import Task  # noqa: E402

A = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/audit")
GOLD = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench/gold")
CLI = os.path.expanduser("~/repo-work/unllm/build/unllm")
PACKS = os.path.expanduser("~/.local/share/unllm/packs")
FULL = os.path.expanduser("~/.local/share/unllm/vectors/wiki-full.unlpq")
DEFAULTS = "kinds,fact,super,roman,phrase,named,howmany,howto,of,rerank,leadfirst,attr,qa,defs,floor,union,floorwide,song,wide"
PER_PACK = 12      # passages sampled per pack
CAP = 2            # questions kept per pack before topping up to 50

WRITE = """Below are {n} numbered passages from the "{pack}" collection. For each passage, write ONE quiz question whose short answer (a number, name, word, command, setting, year or short phrase) the passage states outright.

Rules:
- The question must make sense on its own to someone who has never seen the passage: no "this post", "the author", "the passage", "the user". Name the subject.
- It must have exactly one right answer, and that answer must be written in the passage.
- Prefer specific facts over general knowledge.
- If a passage states no such fact, skip it.
- "pattern" is a case-insensitive Python regex that matches every accepted form of the answer and nothing wrong, written as whole words, e.g. \\b(?:Robert Hooke|Hooke)\\b or \\b(?:4|four)\\b. It must match the passage text.

Passages:
{ps}

Reply with only a JSON list: [{{"passage": N, "question": "...", "answer": "...", "pattern": "..."}}, ...]"""

CHECK = """You are checking quiz questions before they are used as a test. Each item has a question, the answer key, the key's regex pattern, and the source passage.

Keep an item only if ALL hold:
1. The question stands on its own (no reference to "the passage", "this post", "the author", an unnamed person).
2. It has exactly one right answer; it is not contested, opinion, or dependent on time or context.
3. The passage clearly states that answer, and the answer is correct.
4. The pattern accepts the usual correct forms of the answer and would not accept a wrong answer.

Items:
{items}

Reply with only a JSON list: [{{"item": N, "keep": true or false, "why": "a few words"}}, ...]"""

JUDGE = """For each item, decide whether the given answer means the same as the key for that question (a different wording of the same answer counts as right; a different or vaguer answer is wrong).

Items:
{items}

Reply with only a JSON list: [{{"item": N, "right": true or false}}, ...]"""

MEMORY = ('Answer from your own knowledge with the shortest exact answer. Never say you don\'t know.\n\n'
          'Question: {q}\n\nReply with only JSON: {{"answer": "..."}}')


def claude(model, prompt):
    r = subprocess.run(["claude", "-p", "--model", model, prompt], capture_output=True, text=True, timeout=900,
                       stdin=subprocess.DEVNULL, cwd=GOLD)
    m = re.search(r"\[.*\]", r.stdout, re.S)
    try:
        return json.loads(m.group(0)) if m else []
    except ValueError:
        return []


def ollama(q):
    body = json.dumps({"model": "qwen3:4b", "messages": [{"role": "user", "content": MEMORY.format(q=q)}], "stream": False,
                       "format": "json", "think": False, "options": {"temperature": 0, "num_ctx": 8192}}).encode()
    req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            return str(json.loads(json.loads(r.read())["message"]["content"]).get("answer", "")).strip()
    except (ValueError, OSError, KeyError, TypeError):
        return ""


def sample(pack, rng):
    """PER_PACK random passages of at least 300 characters from a pack (any shard)."""
    files = sorted(glob.glob(f"{PACKS}/{pack}/shard-*.sqlite"))
    out, tries = [], 0
    while len(out) < PER_PACK and tries < PER_PACK * 40:
        tries += 1
        con = sqlite3.connect(f"file:{rng.choice(files)}?mode=ro", uri=True)
        top = con.execute("SELECT max(pid) FROM passages").fetchone()[0]
        r = con.execute("SELECT d.title, p.text FROM passages p JOIN docs d ON d.doc_id = p.doc_id WHERE p.pid >= ? "
                        "ORDER BY p.pid LIMIT 1", (rng.randint(0, top),)).fetchone()
        if r and len(r[1]) >= 300 and all(r[0] != t for t, _ in out):
            out.append((r[0], " ".join(r[1].split())[:1200]))
    return out


def valid(pattern, text):
    try:
        return re.search(pattern, text, re.I) is not None
    except re.error:
        return False


def gen(seed=31, name="corpus50"):
    rng = random.Random(seed)
    packs = sorted(os.path.basename(os.path.dirname(p)) for p in glob.glob(f"{PACKS}/*/shard-000.sqlite"))
    log = open(f"{A}/{name}-gen.jsonl", "w")
    bar = Task("corpus50gen", "Corpus questions: write and check", total=len(packs))
    items = []
    for pack in packs:
        ps = sample(pack, rng)
        wrote = claude("claude-haiku-5-5", WRITE.format(n=len(ps), pack=pack,
                                                       ps="\n\n".join(f"[{i}] {t}: {x}" for i, (t, x) in enumerate(ps, 1))))
        cands = []
        for w in wrote:
            try:
                t, x = ps[int(w["passage"]) - 1]
            except (KeyError, ValueError, IndexError, TypeError):
                continue
            c = {"pack": pack, "source": t, "passage": x, "question": str(w.get("question", "")),
                 "answer": str(w.get("answer", "")), "pattern": str(w.get("pattern", ""))}
            c["pattern_in_passage"] = valid(c["pattern"], f"{t}: {x}")
            cands.append(c)
        ok = [c for c in cands if c["pattern_in_passage"]]
        if ok:
            checked = claude("claude-opus-5-5", CHECK.format(items="\n\n".join(
                f"[{i}] question: {c['question']}\nkey: {c['answer']}\npattern: {c['pattern']}\npassage: {c['source']}: {c['passage']}"
                for i, c in enumerate(ok, 1))))
            verdict = {int(v.get("item", 0)): v for v in checked if isinstance(v, dict)}
            for i, c in enumerate(ok, 1):
                c["opus_keep"] = bool(verdict.get(i, {}).get("keep"))
                c["opus_why"] = verdict.get(i, {}).get("why", "no verdict")
        for c in cands:
            log.write(json.dumps(c) + "\n")
        log.flush()
        items += [c for c in ok if c.get("opus_keep")]
        bar.step(detail=f"{pack}: {len(ps)} passages, {len(cands)} written, {sum(c.get('opus_keep', False) for c in cands)} kept")
    bar.finish()

    bar = Task("corpus50probe", "Corpus questions: 4B from memory", total=len(items))
    for c in items:
        c["4b"] = ollama(c["question"])
        c["4b_pattern_right"] = valid(c["pattern"], c["4b"]) if c["4b"] else False
        bar.step(detail=c["pack"])
    bar.finish()
    unsure = [c for c in items if not c["4b_pattern_right"]]
    for k in range(0, len(unsure), 25):
        part = unsure[k:k + 25]
        js = claude("claude-haiku-5-5", JUDGE.format(items="\n\n".join(
            f"[{i}] question: {c['question']}\nkey: {c['answer']}\ngiven answer: {c['4b']}" for i, c in enumerate(part, 1))))
        right = {int(v.get("item", 0)): bool(v.get("right")) for v in js if isinstance(v, dict)}
        for i, c in enumerate(part, 1):
            c["4b_judged_right"] = right.get(i, True)  # no verdict: treat as known, so it is not kept
    for c in items:
        c["4b_wrong"] = not c["4b_pattern_right"] and not c.get("4b_judged_right", True)
    with open(f"{A}/{name}-probe.jsonl", "w") as f:
        f.writelines(json.dumps(c) + "\n" for c in items)

    wrong = [c for c in items if c["4b_wrong"]]
    rng.shuffle(wrong)
    keep, per = [], {}
    for c in wrong:  # up to CAP per pack first, then top up to 50 from what is left
        if per.get(c["pack"], 0) < CAP:
            keep.append(c)
            per[c["pack"]] = per.get(c["pack"], 0) + 1
    keep += [c for c in wrong if c not in keep][:max(0, 50 - len(keep))]
    keep = keep[:50]
    with open(f"{A}/{name}.tsv", "w") as f:
        f.write("id\tquestion\tanswer_pattern\tanswer_type\tsource_title\n")
        f.writelines(f"k{i:03d}\t{c['question']}\t{c['pattern']}\t{c['pack']}\t{c['source']}\n" for i, c in enumerate(keep, 1))
    print(f"written {sum(1 for _ in open(f'{A}/{name}-gen.jsonl'))}, passed Opus {len(items)}, "
          f"4B wrong {len(wrong)}, kept {len(keep)} from {len(set(c['pack'] for c in keep))} packs")
    for i, c in enumerate(keep, 1):
        print(f"  k{i:03d} [{c['pack']}] {c['question']} | key {c['answer']} | 4B said: {c['4b'][:40]}")


def evaluate(reader=False):
    for k in [k for k in os.environ if k.startswith("UNLLM")]:
        del os.environ[k]
    import rerank
    from qa_score import run_unllm
    ranker = rerank.load("qwen3-0.6b")
    rows = [dict(zip(("id", "question", "pattern", "pack", "source"), l.split("\t")))
            for l in open(f"{A}/corpus50.tsv").read().splitlines()[1:]]
    hit = lambda pat, t: bool(t) and valid(pat, t)  # noqa: E731
    if reader:
        from rerank_4b50 import PROMPT, four_b
    out = open(f"{A}/corpus50-eval{'4b' if reader else ''}.jsonl", "w")
    c = dict.fromkeys(("answer in top 10", "top passage holds it, reranked", "4B on top 3" if reader else "plain quote, reranked"), 0)
    bar = Task("corpus50eval", "Corpus questions: unllm + reranker" + (" + 4B" if reader else ""), total=len(rows))
    for q in rows:
        env = {"UNLLM_LAYERS": DEFAULTS + ",meaning", "UNLLM_MEANING": FULL}
        os.environ.update(env)
        ps = [" ".join(p.split()) for p in run_unllm(CLI, q["question"], passages=10)["passages"][:10]]
        for k in env:
            del os.environ[k]
        sc = ranker.scores(q["question"], ps) if ps else []
        rr = [ps[k] for k in sorted(range(len(ps)), key=lambda k: -float(sc[k]))]
        ans = ""
        if rr and reader:
            ans, _ = four_b(PROMPT.format(q=q["question"], ps="\n\n".join(f"[{i}] {p[:900]}" for i, p in enumerate(rr[:3], 1))))
            ans = "" if ans == "NONE" else ans
        elif rr:
            path = f"{A}/tmp/corpus50-rr.txt"
            open(path, "w").write("".join(p + "\n" for p in rr))
            ans = " ".join(subprocess.run([CLI, q["question"]], capture_output=True, text=True, errors="replace", timeout=120,
                                          env=dict(os.environ, UNLLM_PASSAGES=path, UNLLM_LAYERS="")).stdout.split("\n\n")[0].split())
        r = {"answer in top 10": any(hit(q["pattern"], p) for p in ps), "top passage holds it, reranked": bool(rr) and hit(q["pattern"], rr[0]),
             ("4B on top 3" if reader else "plain quote, reranked"): hit(q["pattern"], ans)}
        for k, v in r.items():
            c[k] += v
        out.write(json.dumps(dict(q, **r, answer=ans, top=rr[:3], source_found=any(p.startswith(q["source"] + ":") for p in ps))) + "\n")
        out.flush()
        bar.step(detail=q["id"])
    bar.finish()
    lines = ["\n### Corpus 50: questions from every pack that qwen3:4b gets wrong; full index + reranker + "
             + ("qwen3:4b reading the top 3" if reader else "plain quote") + " (2026-10-20)"]
    lines += [f"- {k}: {v} of 50" for k, v in c.items()]
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


def reread(model):
    from rerank_4b50 import PROMPT
    rows = [json.loads(l) for l in open(f"{A}/corpus50-eval4b.jsonl")]
    tag = model.replace(":", "-")
    if model == "groq":
        import time
        import groq_read
        key = groq_read.api_key()
        last = [0.0]

        def ask_groq(prompt):
            time.sleep(max(0.0, groq_read.PACE - (time.time() - last[0])))
            last[0] = time.time()
            ans, err = groq_read.call(key, "Reply with only JSON.", prompt)
            return ans or ""
    out, right = open(f"{A}/corpus50-reread-{tag}.jsonl", "w"), 0
    bar = Task("corpus50reread", f"Corpus questions: {model} reads", total=len(rows))
    for r in rows:
        prompt = PROMPT.format(q=r["question"], ps="\n\n".join(f"[{i}] {p[:900]}" for i, p in enumerate(r["top"], 1)))
        if model == "groq":
            ans = ask_groq(prompt)
        else:
            body = json.dumps({"model": model, "messages": [{"role": "user", "content": prompt}], "stream": False, "format": "json",
                               "think": False, "options": {"temperature": 0, "num_ctx": 8192}}).encode()
            req = urllib.request.Request("http://localhost:11434/api/chat", data=body, headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(req, timeout=300) as h:
                    ans = str(json.loads(json.loads(h.read())["message"]["content"]).get("answer", "")).strip()
            except (ValueError, OSError, KeyError, TypeError):
                ans = ""
        ans = "" if ans == "NONE" else ans
        ok = bool(ans) and valid(r["pattern"], ans)
        right += ok
        out.write(json.dumps({"id": r["id"], "question": r["question"], "pattern": r["pattern"], "answer": ans, "right": ok,
                              "4b_answer": r["answer"], "4b_right": r["4B on top 3"]}) + "\n")
        out.flush()
        bar.step(detail=r["id"])
    bar.finish()
    lines = [f"\n### Corpus 50, reader only: {model} on the saved reranked top 3 (2026-10-20)",
             f"- {model}: {right} of 50; qwen3:4b on the same passages: {sum(r['4B on top 3'] for r in rows)} of 50"]
    print("\n".join(lines))
    with open(os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md"), "a") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    if sys.argv[1] == "reread":
        reread(sys.argv[2])
        sys.exit()
    if sys.argv[1] == "gen":
        gen(int(sys.argv[2]) if len(sys.argv) > 2 else 31, sys.argv[3] if len(sys.argv) > 3 else "corpus50")
        sys.exit()
    {"eval": evaluate, "eval4b": lambda: evaluate(True)}[sys.argv[1]]()
