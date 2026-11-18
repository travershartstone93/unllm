#!/usr/bin/env python3
"""Inspectable fresh CLI conversations, executable examples, and mode timings."""
from pathlib import Path
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from synthesis.validate import run_isolated


def manual():
    folder=ROOT/".state"/("manual-"+uuid.uuid4().hex[:8]);folder.mkdir(parents=True)
    env={**os.environ,"UNLLM_NO_SERVER":"1","UNLLM_ENGINE":"symbolic",
         "UNLLM_FACT_DB":str(folder/"facts.sqlite"),"UNLLM_PACKS":str(folder/"empty-packs"),
         "UNLLM_MODEL":"this-model-must-never-be-called","UNLLM_ARTIFACTS":str(ROOT/".artifacts")}
    questions=["Maya Chen is in Harborview.","Harborview is in Caldera.","Where is Maya Chen?","Why?",
        "Maya Chen depends on Rowan Patel.","Rowan Patel is not available.","Is Maya Chen blocked?",
        ':complete depends_on "Maya Chen"',"What if Rowan Patel is available?","Is Maya Chen blocked?",
        "No, Rowan Patel is available.","Is Maya Chen blocked?","Maya Chen is not in Harborview.",
        "Is Maya Chen in Harborview?",":undo","Is Maya Chen in Harborview?"]
    result=subprocess.run([str(ROOT/"build/unllm"),"--json"],input="\n".join(questions)+"\n",
                          env=env,text=True,capture_output=True,timeout=30,check=True)
    answers=[json.loads(line) for line in result.stdout.splitlines()]
    assert len(questions)==len(answers)
    for index,truth in ((2,"supported"),(6,"supported"),(8,"refuted"),(9,"supported"),
                        (11,"refuted"),(13,"both"),(15,"supported")):
        assert answers[index].get("truth")==truth,answers[index]
    for question,answer in zip(questions,answers):print(question,"=>",answer["text"],flush=True)
    # A fresh CLI conversation must not inherit those assertions.
    isolated=subprocess.run([str(ROOT/"build/unllm"),"--json","Where is Maya Chen?"],
                            env=env,text=True,capture_output=True,timeout=10,check=True)
    isolated=json.loads(isolated.stdout);assert isolated["status"]=="unsupported",isolated
    prompts=["Write a program that reads positive numbers, triples them, and prints their sum.",
             ':revise {"output":"mean"}',":code python"]
    generated=subprocess.run([str(ROOT/"build/unllm"),"--json","--code","cpp"],
        input="\n".join(prompts)+"\n",env=env,text=True,capture_output=True,timeout=45,check=True)
    programs=[json.loads(line) for line in generated.stdout.splitlines()]
    assert len(programs)==3
    executions=[]
    for program,expected in zip(programs,("33\n","16.5\n","16.5\n")):
        assert program["status"]=="validated",program
        command=["/job/program"] if program["language"]=="cpp" else ["/usr/bin/python3","/job/program.py"]
        result=run_isolated(Path(program["job"]),command,"7\n-2\n4\n0\n")
        assert result.returncode==0 and result.stdout==expected,result
        executions.append({"source":program["source"],"input":"7\n-2\n4\n0\n","expected":expected,
                           "actual":result.stdout,"language":program["language"]})
        print(program["language"],program["meaningful_lines"],program["status"],program["source"],flush=True)
    question="Write a program that reads lines, removes duplicates, and reverses them."
    generated=subprocess.run([str(ROOT/"build/unllm"),"--json","--code","python",question],
                             env=env,text=True,capture_output=True,timeout=20,check=True)
    lines_response=json.loads(generated.stdout)
    if lines_response["status"] in ("ambiguous","unsupported"):
        clarification={"input":"lines","steps":[{"op":"unique"},{"op":"reverse"}],"output":"values"}
        generated=subprocess.run([str(ROOT/"build/unllm"),"--json","--code","python",json.dumps(clarification)],
                                 env=env,text=True,capture_output=True,timeout=20,check=True)
        lines_program=json.loads(generated.stdout)
    else:lines_program=lines_response
    assert lines_program["status"]=="validated",lines_program
    result=run_isolated(Path(lines_program["job"]),["/usr/bin/python3","/job/program.py"],"mango\nkiwi\nmango\nplum\n")
    assert result.returncode==0 and result.stdout=="plum\nkiwi\nmango\n",result
    executions.append({"source":lines_program["source"],"input":"mango\nkiwi\nmango\nplum\n",
                       "expected":"plum\nkiwi\nmango\n","actual":result.stdout,"language":"python"})
    report={"model":"deliberately unavailable; must not be called","conversation":[
        {"question":q,"answer":a} for q,a in zip(questions,answers)],"fresh_context":isolated,
        "program_prompts":prompts+[question],"text_program_initial_response":lines_response,
        "programs":programs+[lines_program],"fresh_executions":executions}
    out=ROOT/".benchmarks/manual_cli.json";out.parent.mkdir(exist_ok=True);out.write_text(json.dumps(report,indent=2)+"\n")
    print("Saved",out,flush=True)


def compare(selected=None):
    questions=["Who wrote Frankenstein?","Which country is Paris in?","Who wrote Frankenstein?","12 times 7"]
    results=[]
    for engine,model in (("legacy","qwen3:4b"),("legacy","off"),("symbolic","off")):
        if selected and engine!=selected:continue
        with tempfile.TemporaryDirectory(dir=ROOT/".state",prefix="mode-") as d:
            folder=Path(d);sock=folder/"service.sock"
            env={**os.environ,"UNLLM_ENGINE":engine,"UNLLM_MODEL":model,"UNLLM_SOCKET":str(sock),"UNLLM_IDLE":"60"}
            env.pop("UNLLM_NO_SERVER",None);env.pop("UNLLM_CODE_LANGUAGE",None)
            startup=time.monotonic()
            log=(folder/"server.stderr").open("w+")
            server=subprocess.Popen([str(ROOT/"build/unllm"),"--serve"],env=env,
                                    stdout=subprocess.DEVNULL,stderr=log)
            try:
                while not sock.exists():
                    if server.poll() is not None:raise RuntimeError("Comparison server exited")
                    if time.monotonic()-startup>25:raise TimeoutError("Comparison startup exceeded 25 seconds")
                    time.sleep(.02)
                startup_ms=(time.monotonic()-startup)*1000
                for index,question in enumerate(questions):
                    started=time.monotonic()
                    try:
                        response=subprocess.run([str(ROOT/"build/unllm"),"--json",question],env=env,
                                                capture_output=True,text=True,timeout=30,check=True)
                        answer=json.loads(response.stdout)
                        record={"status":answer["status"],"text":answer["text"],"answer":answer}
                        assert answer["server"]["pid"]==server.pid,answer
                    except subprocess.TimeoutExpired:
                        record={"status":"timeout","text":"30-second comparison budget exceeded"}
                    record.update(engine=engine,model=model,question=question,cold=index==0,
                                  elapsed_ms=round((time.monotonic()-started)*1000,3),startup_ms=round(startup_ms,3),
                                  executable=str(ROOT/"build/unllm"),server_pid=server.pid)
                    results.append(record)
                    print(engine,model,index,record["status"],record["elapsed_ms"],record["text"][:100],flush=True)
                    if record["status"]=="timeout":break
            finally:
                server.terminate()
                try:server.wait(timeout=3)
                except subprocess.TimeoutExpired:server.kill();server.wait()
                log.close()
    report={"scope":"Four smoke questions per mode, common read-only packs; not an accuracy or representative latency benchmark.",
            "structured_store":str(ROOT/".state/facts.sqlite"),"results":results,
            "created_at":datetime.now(timezone.utc).isoformat(),
            "binary_sha256":hashlib.sha256((ROOT/"build/unllm").read_bytes()).hexdigest()}
    out=ROOT/".benchmarks/mode_comparison.json"
    if selected and out.exists():
        previous=json.loads(out.read_text())
        report["results"]=[r for r in previous["results"] if r["engine"]!=selected]+results
    out.write_text(json.dumps(report,indent=2)+"\n")


if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--compare",action="store_true")
    parser.add_argument("--only-engine",choices=("symbolic","legacy"));args=parser.parse_args()
    compare(args.only_engine) if args.compare else manual()
