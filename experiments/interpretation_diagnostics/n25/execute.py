"""Confirm each exactly interpreted executable request using N23's external oracles."""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/"experiments/algorithm_ir/requirements_replay"))
from replay import ORACLES, cases_for, execute

def verify(arm):
    checks=[]
    with tempfile.TemporaryDirectory() as tmp:
        work=Path(tmp)
        env=dict(os.environ,UNLLM_NO_SERVER="1",UNLLM_MODEL="off",UNLLM_ARTIFACTS=tmp,
                 UNLLM_FACT_DB=str(work/"facts.sqlite"),UNLLM_PYTHON_TARGET="/usr/bin/python3")
        for raw in (HERE/(arm+".jsonl")).read_text().splitlines():
            r=json.loads(raw)
            if not r["correct"]:
                continue
            for target in ("python","cpp"):
                output=subprocess.run([ROOT/"build/unllm","--json","--code",target,json.dumps(r["answer"])],
                                      env=env,capture_output=True,text=True,timeout=60)
                reply=json.loads(output.stdout)
                check=dict(arm=arm,case=r["case"],target=target,status=reply["status"],executions=0)
                if r["case"]<=8:
                    assert reply["status"]=="validated",reply
                    cases=cases_for(ORACLES[r["case"]-1])
                    execute(Path(reply["source"]),target,work,cases,nested=r["case"]==8)
                    check["executions"]=len(cases)
                else:
                    assert reply["status"]==r["answer"]["status"] and "source" not in reply
                checks.append(check)
    return checks

if __name__=="__main__":
    print(json.dumps(verify(sys.argv[1]),indent=2))
