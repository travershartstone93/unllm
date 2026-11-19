"""Verify the authored splits and replay their gold requirements through the native CLI."""
import argparse
import ast
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import operator
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

import jsonschema

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SCHEMA = runpy.run_path(str(HERE.parent / "qwen_contract/trial.py"))["SCHEMA"]
REPLAY = runpy.run_path(str(ROOT / "experiments/algorithm_ir/requirements_replay/replay.py"))
CHECKS = runpy.run_path(str(ROOT / "experiments/algorithm_ir/contract_trial/score.py"))
OPS = dict(gt=operator.gt, ge=operator.ge, lt=operator.lt, le=operator.le, eq=operator.eq)

def read(path):
    return [json.loads(line) for line in path.read_text().splitlines()]

def check_split(rows, positives, questions, stops, repetitions):
    assert Counter(r["expected"]["status"] for r in rows) == dict(ok=positives,clarify=questions,unsupported=stops)
    cells = Counter()
    for row in rows:
        e = row["expected"]
        jsonschema.validate(e, SCHEMA)
        if e["status"] == "ok":
            cells[e["aggregate"], e["comparison"], next(iter(e["compare_to"]))] += 1
            assert e["required_structure"] in ([], ["for"], ["for","if"])
    assert len(cells) == 20 and set(cells.values()) == {repetitions}

def check_sets(folder, held):
    for line in (HERE / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split()
        assert hashlib.sha256((folder / name).read_bytes()).hexdigest() == digest
    train, dev = (read(folder / (k+".jsonl")) for k in ("train", "dev"))
    check_split(train,40,10,10,2)
    check_split(train[:30],20,5,5,1)
    check_split(train[30:],20,5,5,1)
    check_split(dev,20,2,2,1)
    groups = defaultdict(list)
    for r in train:
        groups[r["contrast_id"]].append(r["expected"])
    assert len(groups)==30 and all(len(v)==2 and v[0]!=v[1] for v in groups.values())
    assert {r["contrast_id"] for r in train[:30]}.isdisjoint(r["contrast_id"] for r in train[30:])
    rows = train+dev
    assert len(set(r["id"] for r in rows))==84
    prompts = [" ".join(r["request"].casefold().split()) for r in rows+read(held)]
    assert len(prompts)==len(set(prompts))
    return rows

def check_program(item):
    row, lang = item
    e = row["expected"]
    rec = dict(id=row["id"], target=lang, status=e["status"], passed=0)
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        env = dict(os.environ,UNLLM_NO_SERVER="1",UNLLM_MODEL="off",UNLLM_ARTIFACTS=tmp,
                   UNLLM_FACT_DB=str(work/"facts.sqlite"),UNLLM_PYTHON_TARGET="/usr/bin/python3")
        reply = json.loads(subprocess.check_output([ROOT/"build/unllm","--json","--code",lang,json.dumps(e)],
                                                  cwd=ROOT,env=env,text=True,timeout=90))
        if e["status"] != "ok":
            assert reply["status"]==e["status"] and "source" not in reply, (rec,reply)
            if e["status"]=="clarify":
                assert reply["text"]==e["question"]
            return rec
        assert reply["status"]=="validated" and reply["specification"]["structure"]==e["required_structure"], (rec,reply)
        path = Path(reply["source"])
        k=e["compare_to"].get("literal",3)
        boundary=[k-1,k,k,k+1,k+1]
        inputs=[([],k),(boundary,k-1),(boundary,k),(boundary,k+1),([k,k,k],k),([-10,-3,0,3,10],0),
                ([-10,-3,0,3,10],-3),([0,0,0],0)]
        cases=[]
        for xs,cutoff in inputs:
            threshold=e["compare_to"].get("literal",cutoff)
            matches=[x for x in xs if OPS[e["comparison"]](x,threshold)]
            cases.append((xs,cutoff,len(matches) if e["aggregate"]=="count" else sum(matches)))
        if e["required_structure"] == ["for"]:
            if lang=="python":
                assert any(isinstance(n,ast.For) for n in ast.walk(ast.parse(path.read_text())))
            else:
                tree=json.loads(subprocess.check_output(["clang++","-std=c++20","-fsyntax-only","-Xclang","-ast-dump=json",
                                "-Xclang","-ast-dump-filter=fn0_compute",path]))
                assert any(n.get("kind") in {"CXXForRangeStmt","ForStmt"} for n in CHECKS["children"](tree))
        REPLAY["execute"](path,lang,work,cases,nested=e["required_structure"]==["for","if"])
        rec["passed"]=len(cases)
    return rec

if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("folder",type=Path)
    parser.add_argument("held",type=Path)
    args=parser.parse_args()
    rows=check_sets(args.folder,args.held)
    with ThreadPoolExecutor(max_workers=4) as pool:
        for rec in pool.map(check_program,[(r,lang) for r in rows for lang in ("python","cpp")]):
            print(json.dumps(rec),flush=True)
