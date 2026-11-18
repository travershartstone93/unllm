#!/usr/bin/env python3
"""Publish readable copies of already validated development benchmark outputs."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[1]


def main():
    benchmark=json.loads((ROOT/".benchmarks/synthesis.json").read_text())
    tasks={t["id"]:t for t in json.loads((ROOT/"tests/upgrade_programs.json").read_text())["tasks"]}
    selected=("numeric_summary","record_statistics","record_means")
    folder=ROOT/"examples/upgrade";folder.mkdir(parents=True,exist_ok=True)
    manifest=[]
    for name in selected:
        (folder/(name+".spec.json")).write_text(json.dumps(tasks[name]["spec"],indent=2)+"\n")
        for language,extension in (("python","py"),("cpp","cpp")):
            row=next(r for r in benchmark["results"] if r["task"]==name and r["language"]==language)
            if row["status"]!="validated":raise ValueError("Only validated examples may be exported")
            source=Path(row["source"]);target=folder/(name+"."+extension)
            shutil.copyfile(source,target)
            assert target.read_bytes()==source.read_bytes()
            report=json.loads((Path(row["job"])/"report.json").read_text())
            manifest.append({"task":name,"language":language,"file":target.name,
                "source_sha256":hashlib.sha256(target.read_bytes()).hexdigest(),
                "specification":name+".spec.json","meaningful_lines":row["meaningful_lines"],
                "checks":row["checks"],"toolchain":report["toolchain"],"structure":report["structure"],
                "components":report["construction"]["components"],"validated_job":row["job"]})
    (folder/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    rows=[]
    for name in selected:
        py=next(r for r in manifest if r["task"]==name and r["language"]=="python")
        cpp=next(r for r in manifest if r["task"]==name and r["language"]=="cpp")
        rows.append(f"| [{name}]({name}.spec.json) | [{py['meaningful_lines']} lines]({py['file']}) | [{cpp['meaningful_lines']} lines]({cpp['file']}) | {py['checks']} / {cpp['checks']} |")
    text='''# Generated utility examples

New stateful programs built through the shared constructor are in [the behaviour-contract examples](behaviours/README.md).

Working Python/C++ desktop calculators are in [the calculator examples](calculator/README.md).

These standalone files are exported outputs from the development benchmark, with their typed specifications. The generator never loads them as answer templates. `manifest.json` records source hashes, toolchains, component choices, structure and validation jobs.

| Specification | Python | C++20 | Executable checks (Python / C++) |
| --- | --- | --- | --- |
'''+"\n".join(rows)+'''

Line counts exclude blank and comment lines; C++ includes its necessary preprocessor directives. Python's concise implementations are intentionally shorter. Common input validation, file handling, errors and reporting are shared component code; the operation pipeline is emitted from the specification.

Numeric summary retains values above one, maps `3*x-2`, sorts descending and reports count/sum/min/max/mean. Record statistics filters values below minus four, maps `3*x+1`, deduplicates, sorts and reports each key; malformed rows are skipped. Record means filters values at ten or above, reverses each group and reports its mean; empty groups are omitted.

```sh
python3 examples/upgrade/numeric_summary.py input.txt
clang++ -std=c++20 -O2 examples/upgrade/record_statistics.cpp -o /tmp/unllm-record-statistics
/tmp/unllm-record-statistics records.tsv
./build/unllm --code python --spec examples/upgrade/record_statistics.spec.json
```

The input/output and numeric contracts are documented in [the upgrade guide](../../docs/UPGRADE.md). The files passed the reported fixed and Hypothesis checks; C++ also passed address/undefined-behavior sanitizer executions. This is finite executable validation, not an unrestricted correctness proof. To regenerate the export, run `tools/benchmark_upgrade.py` and then `tools/export_upgrade_examples.py` with the checkout's virtual-environment Python.
'''
    (folder/"README.md").write_text(text)
    print(json.dumps([{k:r[k] for k in ("file","meaningful_lines","checks")} for r in manifest],indent=2))


if __name__=="__main__":main()
