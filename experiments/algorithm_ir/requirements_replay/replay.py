"""Run the N23 primary and transfer arms against the native product CLI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
N19 = HERE.parent / "contract_trial"
SAVED = ROOT / "experiments/interpretation_diagnostics/qwen_contract/big-pickle.jsonl"
ROWS = json.loads((N19 / "cases.json").read_text()) + [
    [[4, 5, 6], 4], [[4, 5, 6], 6], [[-6, -2, 5, 7], 0],
]
# Direct reference functions for the English requests; independent of the JSON arguments.
ABOVE = lambda xs, k: len([x for x in xs if x > k])
BELOW = lambda xs, k: len([x for x in xs if x < k])
ORACLES = [ABOVE, ABOVE, lambda xs, k: len([x for x in xs if x >= k]),
           lambda xs, k: sum(x for x in xs if x > k), BELOW,
           lambda xs, k: len([x for x in xs if x > 5]), ABOVE, BELOW]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cases_for(oracle):
    cases = []
    for xs, k in ROWS:
        y = oracle(xs, k)
        cases.append((xs, k, y if -(2**63) <= y < 2**63 else "integer overflow"))
    return cases


def execute(path, lang, work, cases, nested):
    # Use the existing N19 AST and execution checks only for explicitly requested nesting.
    if nested:
        checks = runpy.run_path(str(N19 / "score.py"))
        if lang == "cpp":
            checks["score_cpp"](path, cases, work)
        else:
            checks["score_python"](path, cases)
        return
    if lang == "python":
        func = runpy.run_path(str(path))["algorithm"]
        for xs, k, expected in cases:
            try:
                value = func(xs.copy(), k)
            except OverflowError as error:
                value = str(error)
            assert type(value) is type(expected) and value == expected
        return
    driver = work / "driver.cpp"
    driver.write_text(path.read_text() + """
#include <iostream>
int main() {
    size_t n;
    I cutoff;
    while (std::cin >> n >> cutoff) {
        Seq values(n);
        for (auto &v : values) std::cin >> v;
        try { std::cout << algorithm(values, cutoff) << '\\n'; }
        catch (const std::overflow_error &e) { std::cout << e.what() << '\\n'; }
    }
}
""")
    binary = work / "driver"
    subprocess.run(["clang++", "-std=c++20", driver, "-o", binary], check=True)
    data = "".join(" ".join(map(str, [len(xs), k, *xs])) + "\n" for xs,k,_ in cases)
    actual = subprocess.check_output([binary], input=data, text=True).splitlines()
    assert actual == [str(y) for _,_,y in cases]


def evaluate(arm):
    result = []
    records = [json.loads(line) for line in SAVED.read_text().splitlines()]
    if arm == "transfer":
        req = dict(status="ok", aggregate="sum", collection="values", comparison="le",
                   compare_to={"literal":5}, required_structure=[])
        records = [dict(case="transfer", content=json.dumps(req))]
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        env = dict(os.environ, UNLLM_NO_SERVER="1", UNLLM_MODEL="off", UNLLM_ARTIFACTS=tmp,
                   UNLLM_FACT_DB=str(work/"facts.sqlite"), UNLLM_PYTHON_TARGET="/usr/bin/python3")
        for row in records:
            i = row["case"]
            req = json.loads(row["content"])
            for lang in ("python", "cpp"):
                before = set(work.glob("job-*"))
                proc = subprocess.run([ROOT/"build/unllm","--json","--code",lang,row["content"]],
                                      env=env,text=True,capture_output=True,timeout=60)
                reply = json.loads(proc.stdout)
                record = dict(case=i, target=lang, status=reply["status"])
                if i in (9,10):
                    assert reply["status"] == req["status"]
                    assert "source" not in reply and set(work.glob("job-*")) == before
                    if i == 9:
                        assert reply["text"] == req["question"]
                    record["text"] = reply["text"]
                else:
                    assert reply["status"] == "validated", reply
                    spec = reply["specification"]
                    assert spec["structure"] == req["required_structure"]
                    oracle = ORACLES[i-1] if arm=="primary" else lambda xs,k: sum(x for x in xs if x<=5)
                    cases = cases_for(oracle)
                    source = Path(reply["source"])
                    execute(source,lang,work,cases,nested=i==8)
                    record.update(executions=len(cases), source_sha256=sha(source))
                result.append(record)
    return dict(worker_sha256=sha(ROOT/"build/native/unllm_symbolic_worker"),
                scorer_sha256=sha(Path(__file__)), rows=result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("arm",choices=["primary","transfer"])
    args=parser.parse_args()
    print(json.dumps(evaluate(args.arm),indent=2))
