"""Development scorer. Run from the checkout with a fresh output directory."""
import csv
import json
import os
from pathlib import Path
import subprocess
import sys

fixtures = Path(__file__).resolve().parent
root = Path.cwd()
out = Path(sys.argv[1]).resolve()
out.mkdir()
subprocess.run(["sha256sum", "-c", "FROZEN.sha256"], cwd=fixtures, check=True)
with (fixtures / "requests.tsv").open() as f:
    tasks = list(csv.DictReader(f, delimiter="\t"))
batch = "\n".join(t["prompt"] for t in tasks) + "\n"
trace = subprocess.run([root / "build/native/algorithm_goal_probe", root], input=batch,
                       text=True, capture_output=True, check=True).stdout
(out / "readings.jsonl").write_text(trace)
readings = [json.loads(line) for line in trace.splitlines()]
assert len(readings) == len(tasks)
env = dict(os.environ, UNLLM_NO_SERVER="1", UNLLM_MODEL="off",
           UNLLM_PYTHON_TARGET="/usr/bin/python3", UNLLM_ARTIFACTS=str(out),
           UNLLM_FACT_DB=str(out / "facts.sqlite"))


def execute_function(source, language, folder):
    if language == "python":
        argv = ["/usr/bin/python3", "-I", "-c",
                "import json,runpy,sys; print(json.dumps(runpy.run_path(sys.argv[1])['algorithm']()))", source]
    else:
        # Choose serialization by actual C++ return type, independently of the label.
        driver = folder / "independent.cpp"
        driver.write_text('#include "algorithm.cpp"\n#include <iostream>\n'
                          'void print(I n){std::cout<<n;}\n'
                          'void print(const Seq &xs){std::cout<<"[";bool first=true;'
                          'for(I x:xs){if(!first)std::cout<<",";first=false;print(x);}std::cout<<"]";}\n'
                          'int main(){print(algorithm());}\n')
        binary = folder / "independent"
        subprocess.run(["/usr/bin/clang++", "-std=c++20", driver, "-o", binary], check=True, timeout=30)
        argv = [binary]
    return json.loads(subprocess.check_output(argv, timeout=10))


results = []
for index, (task, reading) in enumerate(zip(tasks, readings)):
    assert reading["prompt"] == task["prompt"]
    supported = task["first"] != "stop"
    goal, expected = None, None
    if supported:
        first, last = int(task["first"]), int(task["last"])
        goal = dict(program="algorithm_goal", interval=[first, last],
                    map=task["map"], reduce=task["reduce"])
        expected = [x**2 if task["map"] == "square" else x for x in range(first, last+1)]
        if task["reduce"] == "sum":
            expected = sum(expected)
    candidates = [entry["goal"] for entry in reading["candidates"]]
    row = dict(prompt=task["prompt"], goal=goal, expected=expected, candidates=candidates,
               covered=goal in candidates, parse_stop=reading.get("parse_stop"))
    for language in ("python", "cpp"):
        run = subprocess.run([root / "build/unllm", "--json", "--code", language, task["prompt"]],
                             env=env, capture_output=True, text=True, timeout=200, check=True)
        reply = json.loads(run.stdout)
        (out / f"{index}-{language}.json").write_text(run.stdout)
        selected = reply.get("specification")
        decision = dict(status=reply["status"], selected=selected, reason=reply.get("text"))
        if reply["status"] == "validated" and reply.get("kind") == "algorithm":
            decision["actual"] = execute_function(reply["source"], language, Path(reply["job"]))
            decision["matches_oracle"] = supported and decision["actual"] == expected
            decision.pop("reason")
        row[language] = decision
    results.append(row)
    (out / "results.json").write_text(json.dumps(results, indent=2)+"\n")
