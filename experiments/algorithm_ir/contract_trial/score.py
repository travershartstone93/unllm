"""Score native CLI artifacts independently of the construction engine."""
import ast
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ROWS = json.loads((HERE / "cases.json").read_text())
PRIMARY = json.loads((HERE / "primary.json").read_text())


def oracle(values, cutoff, aggregate, relation):
    total = 0
    for value in values:
        if (value > cutoff if relation == "gt" else value >= cutoff):
            total += 1 if aggregate == "count" else value
            if not -(2**63) <= total < 2**63:
                return "integer overflow"
    return total


def children(node):
    yield node
    for child in node.get("inner", []):
        yield from children(child)


def cpp_int(n):
    return f'I(std::stoll("{n}"))'


def score_cpp(path, cases, work):
    dump = subprocess.check_output(["clang++", "-std=c++20", "-fsyntax-only",
        "-Xclang", "-ast-dump=json", "-Xclang", "-ast-dump-filter=fn0_compute", path])
    loops = [n for n in children(json.loads(dump)) if n.get("kind") == "CXXForRangeStmt"]
    assert any(n.get("kind") == "IfStmt" for loop in loops for n in children(loop))
    lines = [path.read_text(), "int main() {"]
    for values, cutoff, expected in cases:
        sequence = "Seq{" + ",".join(map(cpp_int, values)) + "}"
        call = f"algorithm({sequence},{cpp_int(cutoff)})"
        if isinstance(expected, str):
            lines.append(f'try {{ {call}; return 1; }} catch(const std::overflow_error &e) {{ if(std::string(e.what())!="{expected}") return 2; }}')
        else:
            lines.append(f"if ({call} != {cpp_int(expected)}) return 3;")
    harness = work / "score.cpp"
    harness.write_text("\n".join(lines + ["}"]))
    binary = work / "score"
    subprocess.run(["clang++", "-std=c++20", harness, "-o", binary], check=True)
    subprocess.run([binary], check=True)


def score_python(path, cases):
    tree = ast.parse(path.read_text())
    helper = next(n for n in tree.body if getattr(n, "name", "") == "fn0_compute")
    loops = [n for n in ast.walk(helper) if isinstance(n, ast.For)]
    assert any(isinstance(n, ast.If) for loop in loops for n in ast.walk(loop))
    function = runpy.run_path(str(path))["algorithm"]
    for values, cutoff, expected in cases:
        try:
            actual = function(values.copy(), cutoff)
        except OverflowError as error:
            actual = str(error)
        assert actual == expected, (values, cutoff, actual, expected)


def main():
    results = []
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        env = dict(os.environ, UNLLM_NO_SERVER="1", UNLLM_MODEL="off",
                   UNLLM_ARTIFACTS=tmp, UNLLM_FACT_DB=str(work / "facts.sqlite"),
                   UNLLM_PYTHON_TARGET="/usr/bin/python3")
        for aggregate, relation in [("count","gt"), ("sum","gt"), ("count","ge"), ("sum","ge")]:
            spec = json.loads(json.dumps(PRIMARY))
            spec["behavior"]["aggregate"] = aggregate
            spec["behavior"]["where"]["relation"] = relation
            cases = [(xs, k, oracle(xs, k, aggregate, relation)) for xs, k in ROWS]
            for language in ("python", "cpp"):
                report = json.loads(subprocess.check_output([ROOT / "build/unllm",
                    "--json", "--code", language, json.dumps(spec)], env=env))
                assert report["status"] == "validated", report
                path = Path(report["source"])
                if language == "python":
                    score_python(path, cases)
                else:
                    score_cpp(path, cases, work)
                results.append(dict(aggregate=aggregate, relation=relation, language=language,
                                    nested=True, passed=len(cases), candidates=report["transition_candidates"],
                                    source_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
