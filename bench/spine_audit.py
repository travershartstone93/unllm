#!/usr/bin/env python3
"""Spine audit (reports/spine-audit.md): the library CLI on the 400 pilot questions, Simple
Wikipedia shard only, under a named configuration; then the per-stage score.

  spine_audit.py run NAME [--layers LIST] [--debug] [--top N]   -> DATA/audit/NAME.jsonl (resumable)
       LIST is a UNLLM_LAYERS value; "default+extract" adds to the library's default set.
       --debug keeps the library's UNLLM_DEBUG trace (ranks, candidates) with each row.
       --top N saves the whole ranked list, N long, through the UNLLM_TOP measuring switch.
       --passages-from NAME hands each question the 20 passages saved in DATA/vec/NAME.jsonl
       through the UNLLM_PASSAGES measuring door instead of searching.
  spine_audit.py score NAME [NAME ...]                -> prints the stage table, appends it
  --set pilot|dev on either: the 400 trivia questions (default) or the project's own 200
       (tests/clean/dev.tsv, pattern keys, all installed packs)

Stages, right out of 400: F20 = answer in the top 20 passages (whole-word match on the keys);
right; picked-when-found = right among F20; in hand but lost = F20 and wrong, split into
"closest" (the library hedged with "The closest thing I know is") and a wrong answer.
The vector build is never touched; only the shard and the CLI are used.
"""

import concurrent.futures
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import vec_pilot  # noqa: E402
from jobs import DATA  # noqa: E402
from qa_score import nq_right, pattern_right  # noqa: E402
import sets  # noqa: E402
from taskprogress import Task  # noqa: E402

AUDIT = f"{DATA}/audit"
SET = "pilot"  # or "dev": the project's own 200 questions, all packs, pattern keys


def load_items():
    if SET == "dev":
        return [{"id": r["id"], "question": r["question"], "pattern": r["answer_pattern"]} for r in sets.dev_rows()]
    return vec_pilot.load(f"{vec_pilot.VEC}/testset.jsonl")


def right(it, text):
    if not text:
        return False
    return pattern_right(text, it["pattern"]) if "pattern" in it else nq_right(text, it["aliases"])
REPORT = os.path.expanduser("~/Downloads/unllm-corpus-sources/reports/spine-audit.md")
DEFAULT = ("kinds,fact,super,roman,phrase,named,howmany,howto,of,rerank,leadfirst,attr,qa,defs,floor,union,"
           "floorwide,song")
CLOSEST = "The closest thing I know is"


def ask(question, layers, debug, top=20, passages_file=None):
    env = dict(os.environ)
    if SET == "pilot":
        env["UNLLM_PACKS"] = f"{vec_pilot.VEC}/packs-{vec_pilot.PACK}"
    if passages_file:
        env["UNLLM_PASSAGES"] = passages_file
    if layers:
        env["UNLLM_LAYERS"] = layers.replace("default", DEFAULT)
    if debug:
        env["UNLLM_DEBUG"] = "2"
    if top > 20:
        env["UNLLM_TOP"] = str(top)
    r = subprocess.run([vec_pilot.BINARY, "--branch", "--brief", "--passages", str(top), question],
                       capture_output=True, text=True, errors="replace", timeout=180, env=env)
    answer, branch, brief, ps = [], "", [], []
    for line in r.stdout.splitlines():
        if line.startswith("[branch: "):
            branch = line[9:-1]
        elif line.startswith("[brief] "):
            brief.append(line[8:])
        elif line.startswith("[passage] "):
            ps.append(line[10:])
        elif not branch and not line.startswith("  "):
            answer.append(line)
    answer = " ".join(answer).strip()
    if not ps and branch.startswith("world:"):
        ps = [answer] + brief
    return {"answer": answer[:400], "branch": branch, "passages": ps[:top], "debug": r.stderr[-60000:] if debug else ""}


def run(name, layers, debug, top=20, passages_from=None):
    os.makedirs(AUDIT, exist_ok=True)
    given = {}
    if passages_from:
        given = {json.loads(l)["id"]: json.loads(l)["passages"][:20] for l in open(f"{vec_pilot.VEC}/{passages_from}.jsonl")}
        os.makedirs(f"{AUDIT}/tmp", exist_ok=True)
    items = load_items()
    path = f"{AUDIT}/{name}.jsonl"
    done = {r["id"] for r in vec_pilot.load(path)}
    todo = [it for it in items if it["id"] not in done]
    bar = Task(f"audit-{name}", f"Spine audit: {name}", total=len(items))
    bar.step(len(done))

    def one(it):
        pf = None
        if given:
            pf = f"{AUDIT}/tmp/{name}-{it['id']}.txt"
            with open(pf, "w") as g:
                g.write("".join(" ".join(p.split()) + "\n" for p in given.get(it["id"], [])))
        return it, ask(it["question"], layers, debug, top, pf)
    with open(path, "a") as f, concurrent.futures.ThreadPoolExecutor(2) as ex:
        for it, reply in ex.map(one, todo):
            f.write(json.dumps(dict(id=it["id"], layers=layers or "", **reply)) + "\n")
            f.flush()
            bar.step()
    bar.finish()


def score(names):
    items = {it["id"]: it for it in load_items()}
    lines = ["", f"| run ({SET}) | right | F20 | picked when found | closest (in hand) | wrong (in hand) | not in hand |",
             "|---|---|---|---|---|---|---|"]
    for name in names:
        rows = vec_pilot.load(f"{AUDIT}/{name}.jsonl")
        right = f20 = picked = closest = wrong = 0
        for r in rows:
            it = items[r["id"]]
            ok = right(it, r["answer"])
            found = any(right(it, p) for p in r["passages"][:20])
            right += ok
            f20 += found
            picked += ok and found
            if found and not ok:
                if r["answer"].startswith(CLOSEST):
                    closest += 1
                else:
                    wrong += 1
        n = len(rows)
        lines.append(f"| {name} (n={n}) | {right} | {f20} | {picked} of {f20} ({100 * picked / max(f20, 1):.0f}%) | "
                     f"{closest} | {wrong} | {n - f20} |")
    text = "\n".join(lines)
    print(text)
    with open(REPORT, "a") as f:
        f.write(text + "\n")


def main():
    global SET
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    if "--set" in sys.argv:
        SET = sys.argv[sys.argv.index("--set") + 1]
    if sys.argv[1] == "run":
        layers = None
        if "--layers" in sys.argv:
            layers = sys.argv[sys.argv.index("--layers") + 1]
        top = int(sys.argv[sys.argv.index("--top") + 1]) if "--top" in sys.argv else 20
        pfrom = sys.argv[sys.argv.index("--passages-from") + 1] if "--passages-from" in sys.argv else None
        run(sys.argv[2], layers, "--debug" in sys.argv, top, pfrom)
    elif sys.argv[1] == "score":
        score([a for a in sys.argv[2:] if not a.startswith("--") and a not in ("pilot", "dev")])


if __name__ == "__main__":
    main()
