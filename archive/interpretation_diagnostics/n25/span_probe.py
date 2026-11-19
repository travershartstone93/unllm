"""Probe the author's release interface and SCAN domain, without training a parser."""
import ast
import json
import runpy
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPAN = HERE.parents[2] / ".state/n25/span"
CASES = HERE.parent / "qwen_contract/big-pickle.jsonl"
script = ast.parse((SPAN/"span_based/run_span.py").read_text())
domains = next(ast.literal_eval(kw.value) for call in ast.walk(script)
    if isinstance(call,ast.Call) and call.args and isinstance(call.args[0],ast.Constant)
    and call.args[0].value=="--domain" for kw in call.keywords if kw.arg=="choices")
convert = runpy.run_path(str(SPAN/"preprocess/scan/convert_to_sp.py"))["convert_to_lf"]
def probe(work, prompt, gold=""):
    source, dest = work/"input", work/"result"
    source.write_text(f"IN: {prompt} OUT: {gold}\n")
    try:
        convert(str(source),str(dest))
        return json.loads(dest.read_text())["program"]
    except AssertionError:
        return "outside SCAN command vocabulary"

with tempfile.TemporaryDirectory() as d:
    work = Path(d)
    control = probe(work,"jump twice","I_JUMP I_JUMP")
    assert control=="i_twice ( i_jump ( ) )",control
    rows = [json.loads(line) for line in CASES.read_text().splitlines()]
    results = [dict(case=r["case"],scan_probe=probe(work,r["prompt"])) for r in rows]
print(json.dumps(dict(commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=SPAN,text=True).strip(),
                      training_domains=domains, scan_converter_control=control, requests=results,
                      neural_predictions=0),indent=2))
