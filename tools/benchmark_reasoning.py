#!/usr/bin/env python3
"""Development semantics score and separately bounded native latency checks."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import random
import resource
import statistics
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from upgrade.model import Ambiguous,Unsupported,Claim,Goal,RELATIONS
from upgrade.native import NativeEngine
from upgrade.proof import ProofChecker


def distribution(values):
    ordered=sorted(values)
    return {"samples":len(values),"p50_ms":round(statistics.median(values),3),
            "p95_ms":round(ordered[math.ceil(.95*len(ordered))-1],3),"max_ms":round(max(values),3)}


def native_benchmark():
    # 90k independent boolean facts plus 2,500 disjoint four-edge chains.
    claims=[Claim(f"a{i}","available",f"independent{i}","true") for i in range(90000)]
    for group in range(2500):
        claims.extend(Claim(f"d{group}_{i}","depends_on",f"g{group}_{i}",f"g{group}_{i+1}") for i in range(4))
    facts=[c.data() for c in claims];relations=[r.data() for r in RELATIONS]
    engine=NativeEngine();goal=Goal("depends_on","g2499_0","g2499_4")
    started=time.monotonic()
    first=engine.query(facts,relations,[],[goal.data()])
    preparation_ms=(time.monotonic()-started)*1000
    checker=ProofChecker(claims,relations,[])
    assert checker.check(first["solutions"][0]["proofs"])["valid"]
    wall=[];core=[];verification=[]
    for _ in range(60):
        start=time.monotonic();answer=engine.query(facts,relations,[],[goal.data()])
        wall.append((time.monotonic()-start)*1000);core.append(answer["elapsed_us"]/1000)
        start=time.monotonic();checker.check(answer["solutions"][0]["proofs"])
        verification.append((time.monotonic()-start)*1000)
    return {"input_facts":len(facts),"derived_tuples":first["background_size"],
            "topology":"90000 independent availability facts; 2500 disjoint four-edge dependency chains",
            "query":goal.data(),"cold_prepare_and_first_query_ms":round(preparation_ms,3),
            "native_with_explanation":distribution(core),"python_adapter_including_snapshot_hash":distribution(wall),
            "independent_proof_check":distribution(verification),
            "process_peak_rss_kib":resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--tasks",default=str(ROOT/"tests/upgrade_reasoning.json"))
    parser.add_argument("--out",default=str(ROOT/".benchmarks/reasoning.json"))
    parser.add_argument("--native-bench",action="store_true",help=argparse.SUPPRESS)
    args=parser.parse_args()
    if args.native_bench:
        print(json.dumps(native_benchmark()));return
    task_file=Path(args.tasks);before=hashlib.sha256(task_file.read_bytes()).hexdigest()
    tasks=json.loads(task_file.read_text());results=[];rejected=[]
    wall=[];parse=[];core=[]
    with tempfile.TemporaryDirectory() as d:
        engine=Engine(Path(d)/"facts.sqlite");engine.store.import_document(tasks["document"])
        for case in tasks["cases"]:
            start=time.monotonic()
            try:
                response=engine.ask({"question":case["question"],"context":case["id"]})
                elapsed=(time.monotonic()-start)*1000
                if "answers" in case:
                    actual=sorted(row[0] for row in response.get("answers",[]))
                    correct=actual==sorted(case["answers"])
                    if case.get("count"):correct=correct and response.get("known_count")==len(case["answers"])
                else:
                    actual=response.get("truth",response["status"])
                    correct=actual==case["truth"]
                result={"id":case["id"],"question":case["question"],"correct":correct,"actual":actual,
                        "status":response["status"],"text":response["text"],"ms":round(elapsed,3),
                        "answer_asserted":response.get("truth") in ("supported","refuted","both")}
                wall.append(elapsed);parse.append(engine.parser.last_ms);core.append(response.get("elapsed_us",0)/1000)
            except (Unsupported,Ambiguous) as exc:
                result={"id":case["id"],"question":case["question"],"correct":False,
                        "status":type(exc).__name__,"text":str(exc),"answer_asserted":False}
            results.append(result)
            print(case["id"],"pass" if result["correct"] else "FAIL",result["status"],flush=True)
        for question in tasks["must_reject"]:
            try:
                r=engine.ask({"question":question,"context":"reject"})
                rejected.append({"question":question,"rejected":False,"text":r["text"]})
            except (Unsupported,Ambiguous) as exc:
                rejected.append({"question":question,"rejected":True,"text":str(exc)})
        # Fresh generated multiword names exercise linking and topology rather
        # than reusing an encyclopedia answer or a stored response string.
        rng=random.Random(840293)
        generated=[]
        for index in range(8):
            suffix=str(rng.randrange(100000,999999))
            document={"source":{"id":"generated:"+suffix,"title":"Generated names"},
                "entities":[{"id":"g"+suffix+str(i),"name":name+" "+suffix,"type":kind}
                    for i,(name,kind) in enumerate((("Volume","work"),("Writer","person"),("City","city"),("Nation","country")))],
                "claims":[{"predicate":p,"subject":"g"+suffix+str(i),"object":"g"+suffix+str(i+1)}
                           for i,p in enumerate(("author","birthplace","country"))]}
            engine.store.import_document(document)
            q="What is the country of the birthplace of the author of Volume "+suffix+"?"
            try:
                r=engine.ask({"question":q,"context":"generated"+suffix})
                passed=r.get("answers")==[["g"+suffix+"3"]]
                generated.append({"question":q,"correct":passed,"status":r["status"]})
            except (Unsupported,Ambiguous) as exc:
                generated.append({"question":q,"correct":False,"status":type(exc).__name__,"text":str(exc)})
        # Repeat prepared short questions after warming the parser. Exclude
        # first-request startup from this explicitly warm latency distribution.
        warm=[];warm_parse=[];warm_failures=[]
        for i in range(60):
            case=tasks["cases"][i%len(tasks["cases"])]
            if not results[i%len(results)]["correct"]:continue
            start=time.monotonic()
            try:
                engine.ask({"question":case["question"],"context":"timing"})
                warm.append((time.monotonic()-start)*1000);warm_parse.append(engine.parser.last_ms)
            except (Unsupported,Ambiguous) as exc:
                warm_failures.append({"id":case["id"],"status":type(exc).__name__,"text":str(exc)})
        engine.parser.close();engine.store.db.close()
    assert hashlib.sha256(task_file.read_bytes()).hexdigest()==before
    child=subprocess.run([sys.executable,__file__,"--native-bench"],capture_output=True,text=True,timeout=180)
    if child.returncode:raise RuntimeError(child.stderr)
    asserted=[r for r in results if r["answer_asserted"]]
    report={"tasks_sha256":before,"blinded":False,"scored":len(results),
            "exact_correct":sum(r["correct"] for r in results),
            "correct_rate":sum(r["correct"] for r in results)/len(results),
            "answered_precision":sum(r["correct"] for r in asserted)/len(asserted) if asserted else None,
            "answer_coverage":len(asserted)/len(results),
            "clarification_rate":sum(r["status"]=="Ambiguous" for r in results)/len(results),
            "false_assertions":[r["id"] for r in asserted if not r["correct"]],
            "warm_end_to_end":distribution(warm),"warm_parser":distribution(warm_parse),
            "warm_failures":warm_failures,
            "native_100k":json.loads(child.stdout),"results":results,"rejection_cases":rejected,
            "generated_name_cases":generated}
    out=Path(args.out);out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:v for k,v in report.items() if k not in ("results","rejection_cases","generated_name_cases")},indent=2))


if __name__=="__main__":main()
