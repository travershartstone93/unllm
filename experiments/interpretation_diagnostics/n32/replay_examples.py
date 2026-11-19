"""Check actual Qwen adapter outputs through both native CLI targets."""
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
from fetch import OUT

ROOT=Path(__file__).resolve().parents[3]
CHECK=runpy.run_path(str(ROOT/"experiments/algorithm_ir/requirements_replay/replay.py"))["execute"]
ROWS=[json.loads(s) for s in (OUT/"finetuned/qwen/dev-constrained.jsonl").read_text().splitlines()]
# Independently specified witnesses for a correct signed threshold and a reversed comparison.
WITNESSES={"d16":([-4,-1,2],5,-4,-4),"d15":([2,9],5,2,9)}
records=[]
with tempfile.TemporaryDirectory() as temp:
    work=Path(temp)
    env=dict(os.environ,UNLLM_NO_SERVER="1",UNLLM_MODEL="off",UNLLM_ARTIFACTS=temp,
             UNLLM_FACT_DB=str(work/"facts.sqlite"),UNLLM_PYTHON_TARGET="/usr/bin/python3")
    for row in ROWS:
        if row["id"] not in WITNESSES:continue
        xs,cutoff,wanted,predicted=WITNESSES[row["id"]]
        for lang in ("cpp","python"):
            reply=json.loads(subprocess.check_output([ROOT/"build/unllm","--json","--code",lang,json.dumps(row["answer"])],
                                                     cwd=ROOT,env=env,text=True,timeout=90))
            assert reply["status"]=="validated"
            CHECK(Path(reply["source"]),lang,work,[(xs,cutoff,predicted)],nested=False)
            records.append(dict(id=row["id"],request=row["request"],target=lang,values=xs,cutoff=cutoff,
                                expected_from_english=wanted,verified_result=predicted))
assert len(records)==4
print(json.dumps(records,indent=2))
