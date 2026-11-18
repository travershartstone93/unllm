#!/usr/bin/env python3
"""Actual CLI requests followed by independent Qt keyboard/mouse workflows."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from synthesis.apps.validate import sandbox


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--reuse-report",type=Path,help="Probe already generated, source-hash-checked jobs")
    args=parser.parse_args()
    requests_path=ROOT/"tests/upgrade_requests.json"
    digest=hashlib.sha256(requests_path.read_bytes()).hexdigest()
    cases_path=ROOT/"tests/upgrade_request_interactions.json";cases=json.loads(cases_path.read_text())
    if args.reuse_report:
        generated=json.loads(args.reuse_report.read_text())
        if generated["requests_sha256"]!=digest:raise ValueError("The request fixture changed")
    else:
        folder=ROOT/".state/request-verification";folder.mkdir(exist_ok=True)
        generated={"requests_sha256":digest,"results":[]}
        env=dict(os.environ,UNLLM_NO_SERVER="1",UNLLM_MODEL="off",UNLLM_FACT_DB=str(folder/"facts.sqlite"))
        for task in json.loads(requests_path.read_text()):
            for language in ("python","cpp"):
                start=time.monotonic()
                process=subprocess.run([str(ROOT/"build/unllm"),"--json","--code",language,task["question"]],
                                       env=env,text=True,capture_output=True,timeout=190,cwd=ROOT)
                response=json.loads(process.stdout)
                generated["results"].append({**task,"language":language,"seconds":round(time.monotonic()-start,3),
                                              "returncode":process.returncode,"response":response})
                print("generate",task["id"],language,response.get("status"),flush=True)
        (ROOT/".benchmarks/request_fix_trial.json").write_text(json.dumps(generated,indent=2,ensure_ascii=False)+"\n")
    results=[]
    for row in generated["results"]:
        response=row["response"];language=row["language"];start=time.monotonic()
        result={"task":row["id"],"language":language,"question":row["question"],"generation_status":response["status"],
                "generation_seconds":row["seconds"],"job":response.get("job"),"source":response.get("source")}
        if response["status"]!="validated":
            result.update(passed=False,error=response.get("text"))
        else:
            job=Path(response["job"]);source=Path(response["source"])
            if hashlib.sha256(source.read_bytes()).hexdigest()!=response["source_sha256"][source.name]:
                raise ValueError("The generated source changed: "+str(source))
            if language=="python":
                shutil.copyfile(ROOT/"tools/probes/request_python.py",job/"independent_probe.py")
                command=["/runtime/python/bin/python3.12","/job/independent_probe.py"]
            else:
                shutil.copyfile(ROOT/"tools/probes/request_cpp.cpp",job/"independent_probe.cpp")
                flags=subprocess.check_output(["pkg-config","--cflags","--libs","Qt6Widgets","Qt6Test"],text=True).split()
                command=["clang++","-std=c++20","-O0",str(job/"independent_probe.cpp"),*flags,"-o",str(job/"independent_probe")]
                compilation=subprocess.run(command,text=True,capture_output=True,timeout=60)
                if compilation.returncode:
                    result.update(passed=False,error=compilation.stderr[-8000:])
                    results.append(result);print("probe",row["id"],language,"compile failed",flush=True);continue
                command=["/job/independent_probe"]
            output=job/"independent-output";output.mkdir(exist_ok=True)
            execution=sandbox(job,command,cases[row["id"]],output,python=language=="python",timeout=30)
            result["returncode"]=execution.returncode
            if execution.returncode:
                result.update(passed=False,error=execution.stderr[-6000:],stdout=execution.stdout[-1000:])
            else:
                result.update(json.loads(execution.stdout))
                result["preview"]=str(output/"independent-preview.png")
            result["source_sha256"]=hashlib.sha256(source.read_bytes()).hexdigest()
        result["probe_seconds"]=round(time.monotonic()-start,3);results.append(result)
        print("probe",row["id"],language,result.get("passed"),flush=True)
        for check in result.get("checks",[]):
            if not check["passed"]:print("failure",json.dumps(check,ensure_ascii=False),flush=True)
        report={"requests_sha256":digest,"workflows_sha256":hashlib.sha256(cases_path.read_bytes()).hexdigest(),
                "method":"fresh CLI generation plus independent Qt controls and file assertions; no built-in scenario/oracle",
                "results":results,"passed":sum(r.get("passed",False) for r in results),"attempted":len(results)}
        (ROOT/".benchmarks/request_fix_verified.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    if len(results)!=12 or not all(row.get("passed") for row in results):raise SystemExit(1)
    print("Independent workflows passed: 12/12",flush=True)


if __name__=="__main__":main()
