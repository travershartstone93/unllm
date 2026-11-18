#!/usr/bin/env python3
"""Run frozen executable-contract tasks; keep reports outside source files."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import statistics
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--tasks",default=str(ROOT/"tests/upgrade_programs.json"))
    parser.add_argument("--out",default=str(ROOT/".benchmarks/synthesis.json"))
    args=parser.parse_args()
    task_file=Path(args.tasks)
    before=hashlib.sha256(task_file.read_bytes()).hexdigest()
    tasks=json.loads(task_file.read_text())["tasks"]
    results=[]
    with tempfile.TemporaryDirectory() as temporary:
        engine=Engine(Path(temporary)/"facts.sqlite")
        for task in tasks:
            for language in ("python","cpp"):
                start=time.monotonic()
                try:
                    response=engine.ask({"spec":task["spec"],"language":language,"context":"benchmark"})
                    result={"task":task["id"],"language":language,"status":response["status"],
                            "source":response["source"],"meaningful_lines":response["meaningful_lines"],
                            "checks":response["attempts"][-1].get("cases",0),"job":response["job"]}
                except Exception as exc:
                    result={"task":task["id"],"language":language,"status":"failed","error":str(exc)}
                result["seconds"]=round(time.monotonic()-start,3)
                results.append(result)
                print(task["id"],language,result["status"],result["seconds"],flush=True)
        engine.parser.close()
    assert hashlib.sha256(task_file.read_bytes()).hexdigest()==before
    totals={}
    for language in ("python","cpp"):
        rows=[r for r in results if r["language"]==language]
        good=[r for r in rows if r["status"]=="validated"]
        totals[language]={"attempted":len(rows),"validated":len(good),
                         "success_rate":len(good)/len(rows),
                         "median_seconds":statistics.median(r["seconds"] for r in rows),
                         "meaningful_lines":[r["meaningful_lines"] for r in good]}
    report={"tasks_sha256":before,"blinded":False,"totals":totals,"results":results}
    out=Path(args.out);out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(totals,indent=2),flush=True)


if __name__=="__main__":main()
