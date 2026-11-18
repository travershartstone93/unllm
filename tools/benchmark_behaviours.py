#!/usr/bin/env python3
"""Measure composition and check new outcomes after construction."""
from pathlib import Path
import hashlib
import json
import secrets
import statistics
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from synthesis.behaviour.spec import BehaviourSpec
from synthesis.behaviour.validate import run_cases


def fresh_task():
    # Chosen when the benchmark runs; neither this program label nor these
    # coefficients are registered in the construction catalog.
    coefficient=secrets.choice([2,3,4,5,7]);offset=secrets.randbelow(18)+3
    name="new_goal_"+secrets.token_hex(4)
    source=secrets.randbelow(100)+11
    return {"id":"new_requirements","spec":{"schema":2,"program":name,"title":"Fresh Requirements",
        "state":{"quantity":{"type":"number","initial":0},"text":{"type":"text","initial":""}},
        "views":[{"id":"quantity","kind":"number_input","state":"quantity"},
                 {"id":"text","kind":"text_edit","state":"text"},
                 {"id":"characters","kind":"label","value":"chars(text)"}],
        "actions":[{"id":"solve","ensure":str(coefficient)+" * next.quantity == prev.quantity + "+str(offset)},
                   {"id":"transform","assign":{"text":"reverse_lines(upper(strip(text)))"}}]},
        "fresh":[{"initial":{"quantity":source},"events":[{"button":"solve"},{"input":"text","text":"  kiwi\nmango  "},{"button":"transform"}],
                  "expect":{"state":{"quantity":(source+offset)/coefficient,"text":"MANGO\nKIWI"}}}],
        "coefficients":{"coefficient":coefficient,"offset":offset,"source":source}}


def main():
    task_file=ROOT/"tests/upgrade_behaviours.json";before=hashlib.sha256(task_file.read_bytes()).hexdigest()
    tasks=json.loads(task_file.read_text())["tasks"]+[fresh_task()]
    folder=ROOT/".state/behaviour-benchmark";folder.mkdir(exist_ok=True)
    engine=Engine(folder/"facts.sqlite");rows=[]
    try:
        for task in tasks:
            for language in ("python","cpp"):
                start=time.monotonic()
                result=engine.ask({"spec":task["spec"],"language":language,"context":"matrix"})
                seconds=round(time.monotonic()-start,3)
                row={"task":task["id"],"language":language,"status":result["status"],"seconds":seconds,
                     "job":result.get("job"),"source":result.get("source"),"specification":task["spec"],
                     "checks":result.get("attempts",[{}])[0].get("cases",0),"fresh_cases":task["fresh"]}
                if result["status"]=="validated":
                    checked=run_cases(BehaviourSpec.from_dict(task["spec"]),Path(result["job"]),language,task["fresh"],time.monotonic()+40)
                    row["fresh_passed"]=checked["valid"]
                    if not checked["valid"]:row["status"]="failed";row["fresh_failure"]=checked
                else:row["failure"]=result.get("attempts",result)
                rows.append(row)
                print(task["id"],language,row["status"],seconds,flush=True)
    finally:engine.parser.close();engine.store.db.close()
    totals={language:{"attempted":len(selected),"validated":sum(r["status"]=="validated" for r in selected),
                      "median_seconds":statistics.median(r["seconds"] for r in selected)}
            for language in ("python","cpp") if (selected:=[r for r in rows if r["language"]==language])}
    report={"blinded":False,"purpose":"bounded compositional construction, not unrestricted programming ability",
            "tasks_sha256":before,"fresh_task":tasks[-1],"results":rows,"totals":totals}
    assert hashlib.sha256(task_file.read_bytes()).hexdigest()==before
    path=ROOT/".benchmarks/behaviours.json";path.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    print(json.dumps(totals,indent=2))
    if any(r["status"]!="validated" for r in rows):raise SystemExit(1)


if __name__=="__main__":main()
