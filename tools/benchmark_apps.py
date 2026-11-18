#!/usr/bin/env python3
"""Development application matrix and separate, freshly chosen GUI actions."""
from pathlib import Path
import hashlib
import json
import os
import sys
import time
import statistics

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from synthesis.apps.validate import sandbox


def main():
    task_file=ROOT/"tests/upgrade_apps.json";before=hashlib.sha256(task_file.read_bytes()).hexdigest()
    tasks=json.loads(task_file.read_text())["tasks"]
    folder=ROOT/".state/application-benchmark";folder.mkdir(parents=True,exist_ok=True)
    engine=Engine(folder/"facts.sqlite")
    rows=[]
    try:
        for task in tasks:
            for language in ("python","cpp"):
                start=time.monotonic()
                result=engine.ask({"spec":task["spec"],"language":language,"context":"application-benchmark"})
                row={"task":task["id"],"language":language,"status":result["status"],"seconds":round(time.monotonic()-start,3),
                     "job":result["job"],"source":result["source"],"preview":result["preview"],
                     "application_lines":result["meaningful_lines"],"structure":result["structure"]}
                validation=result["attempts"][0]
                row["checks"]=validation.get("cases",0)
                row["gui_scenarios"]=validation.get("gui_scenarios",0)
                # A second, fresh input set is not fed into construction/repair.
                if result["status"]=="validated":
                    if task["id"]=="mouse_only":
                        events=[{"button":b} for b in ("digit_8","multiply","digit_7","evaluate")];expected="56"
                    else:
                        events=[{"text":"46+19"},{"key":"Return"}];expected="65"
                    if task["id"]=="memory":
                        events += [{"button":"memory_add"},{"button":"clear"},{"text":"8"},{"button":"memory_subtract"},{"button":"memory_recall"}]
                        expected="57"
                    command=["/runtime/python/bin/python3.12","/job/gui_driver.py"] if language=="python" else ["/job/gui_driver"]
                    process=sandbox(Path(result["job"]),command,{"expressions":[],"gui":[{"events":events}]},
                                    Path(result["job"])/"test-output",python=language=="python")
                    actual=json.loads(process.stdout)["gui"][0]["display"] if process.returncode==0 else process.stderr
                    row["fresh_gui"]={"expected":expected,"actual":actual,"passed":actual==expected}
                    if actual!=expected:row["status"]="failed"
                else:row["failure"]=validation
                rows.append(row)
                print(task["id"],language,row["status"],row["seconds"],flush=True)
        # Exercise both English creation and the existing interactive revision
        # protocol, rather than scoring only normalized JSON inputs.
        requests=[
            {"question":"Make a standard calculator app.","language":"python","context":"fresh-app"},
            {"question":':revise {"memory":true,"operations":["add","subtract"]}',"context":"fresh-app"},
            {"question":":code cpp","context":"fresh-app"}]
        dialogue=[]
        for request in requests:
            response=engine.ask(request)
            dialogue.append({"request":request,"status":response["status"],"job":response["job"],
                             "language":response["language"],"specification":response["specification"]})
            print("dialogue",response["language"],response["status"],flush=True)
    finally:
        engine.parser.close();engine.store.db.close()
    assert hashlib.sha256(task_file.read_bytes()).hexdigest()==before
    totals={}
    for language in ("python","cpp"):
        selected=[r for r in rows if r["language"]==language]
        totals[language]={"attempted":len(selected),"validated":sum(r["status"]=="validated" for r in selected),
                          "median_seconds":statistics.median(r["seconds"] for r in selected)}
    report={"blinded":False,"tasks_sha256":before,"totals":totals,"results":rows,"dialogue":dialogue}
    path=ROOT/".benchmarks/applications.json";path.parent.mkdir(exist_ok=True);path.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(totals,indent=2))


if __name__=="__main__":main()
