#!/usr/bin/env python3
"""Goal-driven Prolog comparison on exactly the 100k dependency fixture."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.model import Claim
from tools.benchmark_reasoning import distribution


def main():
    claims=[Claim(f"a{i}","available",f"independent{i}","true") for i in range(90000)]
    for group in range(2500):
        claims.extend(Claim(f"d{group}_{i}","depends_on",f"g{group}_{i}",f"g{group}_{i+1}") for i in range(4))
    with tempfile.TemporaryDirectory() as d:
        path=Path(d)/"facts.json";path.write_text(json.dumps([c.data() for c in claims]))
        result=subprocess.run([str(ROOT/".deps/bin/swipl"),"-q","-s",
            str(ROOT/"reasoning/prolog/benchmark.pl"),"--",str(path)],text=True,capture_output=True,timeout=180)
    if result.returncode:raise RuntimeError(result.stderr)
    data=json.loads(result.stdout)
    assert data["proof_ids"]==[f"d2499_{i}" for i in range(4)]
    report={"input_facts":len(claims),"load_and_first_query_ms":data["prepare_and_first_query_ms"],
            "warm_table_and_source_path":distribution(data["times_ms"]),
            "process_peak_rss_kib":data["process_peak_rss_kib"],
            "proof_ids":data["proof_ids"],
            "scope":"Same 100k inputs and four-edge dependency query as benchmark_reasoning.py. Goal-driven SWI tables; no unused type closure.",
            "measurement":"Warm samples average 100 truth-and-path queries inside SWI. Hybrid samples include C dispatch and Souffle proof serialization. Neither comparison establishes a general speed advantage."}
    path=ROOT/".benchmarks/prolog_only.json";path.parent.mkdir(exist_ok=True);path.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))


if __name__=="__main__":main()
