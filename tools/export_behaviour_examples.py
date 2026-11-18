#!/usr/bin/env python3
"""Export outputs and hashes from the contract-composition benchmark."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT=Path(__file__).resolve().parents[1]
FOLDER=ROOT/"examples/upgrade/behaviours"


def main():
    report=json.loads((ROOT/".benchmarks/behaviours.json").read_text())
    FOLDER.mkdir(parents=True,exist_ok=True);manifest=[]
    for row in report["results"]:
        if row["status"]!="validated" or not row.get("fresh_passed"):raise ValueError("Only validated outputs may be exported.")
        folder=FOLDER/row["task"];folder.mkdir(exist_ok=True)
        source=Path(row["source"]);job=Path(row["job"]);job_report=json.loads((job/"report.json").read_text())
        expected=job_report["source_sha256"][source.name]
        if hashlib.sha256(source.read_bytes()).hexdigest()!=expected:raise ValueError("Validated source changed.")
        target=folder/source.name;shutil.copyfile(source,target)
        (folder/"specification.json").write_text(json.dumps(row["specification"],indent=2,ensure_ascii=False)+"\n")
        if row["language"]=="python":
            shutil.copyfile(job/"requirements.txt",folder/"requirements.txt")
            shutil.copyfile(job_report["preview"],folder/"preview.png")
        else:
            cache=ROOT/".state/behaviour-examples"/row["task"];cache.mkdir(parents=True,exist_ok=True)
            shutil.copy2(job/"program",cache/"program");(cache/"program").touch()
        validation=job_report["attempts"][0]
        manifest.append({"task":row["task"],"language":row["language"],"file":str(target.relative_to(FOLDER)),
                         "source_sha256":expected,"validated_job":row["job"],"cases":validation["cases"],
                         "gui_scenarios":validation["gui_scenarios"],"sanitizers":validation["sanitizers"],
                         "fresh_passed":True,"solver":job_report["solver"],
                         "application_name_used":job_report["construction"]["application_name_used"],
                         "structure":job_report["structure"]})
    (FOLDER/"manifest.json").write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+"\n")
    (FOLDER/"README.md").write_text("""# Programs constructed from behaviour contracts

These Python/PySide6 and C++/Qt outputs use the same construction path.
There is no application-category registration for these programs. Their
specifications declare typed state, observable views and required action outcomes.
The program label is metadata. The examples are never read by the generator.

Run from the upgrade checkout:

    ./examples/upgrade/behaviours/run.sh mixed_workspace python
    ./examples/upgrade/behaviours/run.sh conversion cpp
    ./examples/upgrade/behaviours/run.sh text_workbench python
    ./examples/upgrade/behaviours/run.sh paired_state cpp
    ./examples/upgrade/behaviours/run.sh new_requirements python

Each folder has both source languages, its specification and a screenshot.
The mixed workspace combines editing, derived word counts, a numeric transition,
Boolean state, a text transformation and saving. Conversion and paired state
require relational scalar synthesis, including simultaneous updates.
The final composition's label and coefficients were chosen when the benchmark ran.

The launcher needs a desktop display and the checkout's pinned Python environment
or host Qt6 runtime. C++ can rebuild from the exported source using Clang and Qt6
Widgets development files. Python can run separately with its requirements.txt.
Neither generated language needs unllm, Prolog, cvc5 or a model at application runtime.

Regenerate from a specification:

    ./build/unllm --code python --spec examples/upgrade/behaviours/mixed_workspace/specification.json

The manifest records source hashes, solver results, structure and executable
checks. All outputs passed the ordinary validation and additional fresh inputs;
C++ passed normal and sanitizer executions. This is development evidence of
bounded composition, not unrestricted programming ability or a blind evaluation.
See [the architecture report](../../../docs/BEHAVIOUR_CONSTRUCTION.md).

Reproduce with .venv/bin/python tools/benchmark_behaviours.py followed by
.venv/bin/python tools/export_behaviour_examples.py.
""")
    print(json.dumps([{k:r[k] for k in ("task","language","cases")} for r in manifest],indent=2))


if __name__=="__main__":main()
