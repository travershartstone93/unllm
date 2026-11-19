"""Replay the three reviewed CoNaLa labels against their snippets and both unLLM targets."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

import jsonschema

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
FOLDER = Path(sys.argv[1]).resolve()
TRAIN_SHA = "f25078347c2738c3308e2a1e6aa90dc0c944febfee083dd073e26a68d3747bb7"

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read_lines(path):
    return [json.loads(line) for line in path.read_text().splitlines()]

train = FOLDER / "train.json"
if digest(train) != TRAIN_SHA:
    raise ValueError("Unexpected training file")
for entry in (HERE / "frozen.sha256").read_text().splitlines():
    checksum, name = entry.split(maxsplit=1)
    path = FOLDER / "holdout.jsonl" if Path(name).name == "holdout.jsonl" else HERE / "protocol.md"
    if digest(path) != checksum:
        raise ValueError(f"Changed frozen file {path}")

schema = runpy.run_path(str(HERE.parent / "qwen_contract/trial.py"))["SCHEMA"]
replay = runpy.run_path(str(ROOT / "archive/algorithm_ir/requirements_replay/replay.py"))
rows = read_lines(train)
holdout = read_lines(FOLDER / "holdout.jsonl")
def normalized(s):
    return " ".join(s.casefold().split())
source_prompts = {normalized(row[key]) for row in rows for key in ("intent", "rewritten_intent") if row[key]}
frozen_prompts = {normalized(row["request"]) for row in holdout}
assert len(frozen_prompts) == len(holdout) and frozen_prompts.isdisjoint(source_prompts)
for row in holdout:
    jsonschema.validate(row["expected"], schema)

# One-based positions in the digest-pinned split. Only these reviewed snippets are run.
matches = []
for n in (948, 949, 950):
    source = rows[n - 1]
    aliases = {"j": "values"}
    if n == 950:
        aliases["i"] = "cutoff"
    req = dict(status="ok", aggregate="count", collection="values", comparison="gt",
               compare_to={"input": "cutoff"} if n == 950 else {"literal": 5}, required_structure=[])
    jsonschema.validate(req, schema)
    matches.append(dict(row=n, question_id=source["question_id"], request=source["rewritten_intent"],
                        snippet=source["snippet"], aliases=aliases, requirements=req))

inputs = replay["ROWS"] + [[[4, 5, 6], 5], [[5, 5, 5], 4], [[5, 5, 5], 5]]
def cases_for(match):
    runtime = "input" in match["requirements"]["compare_to"]
    return [(xs, k, len([x for x in xs if x > (k if runtime else 5)])) for xs, k in inputs]

source_check = """
import ast, json, sys
import numpy as np
for row in json.load(sys.stdin):
    tree = ast.parse(row["snippet"])
    prefix = ast.Module(body=tree.body[:-1], type_ignores=[])
    expr = ast.Expression(tree.body[-1].value)
    for xs, k, expected in row["cases"]:
        names = dict(np=np, j=xs, i=k)
        exec(compile(prefix, '<reviewed>', 'exec'), names)
        value = eval(compile(expr, '<reviewed>', 'eval'), names)
        assert isinstance(value, (int, np.integer)) and value == expected
"""
sandbox = ["bwrap", "--die-with-parent", "--unshare-all", "--ro-bind", "/usr", "/usr",
           "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib", "/lib64", "--proc", "/proc",
           "--dev", "/dev", "--tmpfs", "/tmp", "--chdir", "/tmp", "--clearenv",
           "--setenv", "OPENBLAS_NUM_THREADS", "1", "/usr/bin/python3", "-I", "-c", source_check]
payload = [dict(snippet=m["snippet"], cases=cases_for(m)) for m in matches]
subprocess.run(sandbox, input=json.dumps(payload), text=True, check=True, timeout=30)

runs = []
with tempfile.TemporaryDirectory() as tmp:
    work = Path(tmp)
    env = dict(os.environ, UNLLM_NO_SERVER="1", UNLLM_MODEL="off", UNLLM_ARTIFACTS=tmp,
               UNLLM_FACT_DB=str(work / "facts.sqlite"), UNLLM_PYTHON_TARGET="/usr/bin/python3")
    for match in (matches[0], matches[2]):
        for lang in ("cpp", "python"):
            reply = subprocess.run([ROOT / "build/unllm", "--json", "--code", lang, json.dumps(match["requirements"])],
                                   env=env, capture_output=True, text=True, timeout=60, check=True)
            result = json.loads(reply.stdout)
            assert result["status"] == "validated", result
            cases = cases_for(match)
            replay["execute"](Path(result["source"]), lang, work, cases, nested=False)
            runs.append(dict(compare_to=match["requirements"]["compare_to"], target=lang, passed=len(cases)))

(FOLDER / "direct.jsonl").write_text("".join(json.dumps(m) + "\n" for m in matches))
print(json.dumps(dict(train_sha256=TRAIN_SHA, train_rows=len(rows), question_ids=len({r["question_id"] for r in rows}),
                      source_rows=[m["row"] for m in matches], inputs_per_snippet=len(inputs),
                      snippet_executions=len(matches)*len(inputs), targets=runs, frozen_valid=len(holdout),
                      frozen_exact_overlaps=0, worker_sha256=digest(ROOT / "build/native/unllm_symbolic_worker")), indent=2))
