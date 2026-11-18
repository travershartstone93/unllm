"""Application construction jobs share the existing budget and artifact API."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import time
import uuid
from .planner import construct_plan
from .construct import construct
from . import emit
from .validate import validate,cpp_command,DEPS
from upgrade.model import Unsupported

ROOT=Path(__file__).resolve().parents[2]


def build(spec,language,request,context,engine,started,deadline):
    if not (DEPS/"generated/manifest.json").exists():
        raise Unsupported("Application dependencies are missing; run .venv/bin/python tools/setup_apps.py.")
    construction=construct_plan(spec,engine.native,language)
    app,solver=construct(spec,construction)
    job=Path(os.environ.get("UNLLM_ARTIFACTS",ROOT/".artifacts"))/("job-"+uuid.uuid4().hex[:12])
    job.mkdir(parents=True,exist_ok=False);job=job.resolve()
    source=job/("program.py" if language=="python" else "program.cpp")
    source.write_text(emit.python(app,spec) if language=="python" else emit.cpp(app,spec))
    folder=DEPS/"generated"/("python" if language=="python" else "cpp")
    suffixes=(".py",) if language=="python" else (".cpp",".h")
    for path in folder.iterdir():
        if path.suffix in suffixes:shutil.copyfile(path,job/path.name)
    shutil.copyfile(Path(__file__).parent/"Arithmetic.g4",job/"Arithmetic.g4")
    shutil.copyfile(DEPS/"antlr-source/LICENSE.txt",job/"ANTLR-LICENSE.txt")
    if language=="cpp":subprocess.run(["clang-format","-i",str(source)],check=True,timeout=5)
    if language=="python":
        (job/"requirements.txt").write_text("PySide6-Essentials==6.12.0\nshiboken6==6.12.0\nantlr4-python3-runtime==4.13.2\n")
    (job/"application.ir.json").write_text(json.dumps(app.data(),indent=2,ensure_ascii=False)+"\n")
    (job/"specification.json").write_text(json.dumps(spec.data(),indent=2)+"\n")
    try:validation=validate(spec,app,job,language,deadline)
    except subprocess.TimeoutExpired:
        validation={"valid":False,"stage":"budget","message":"Application construction or validation exceeded its time budget."}
    except (OSError,RuntimeError,ValueError) as error:
        validation={"valid":False,"stage":"validation","message":str(error)}
    valid=validation["valid"]
    if language=="python":
        run=shlex.join([str(ROOT/".venv/bin/python"),str(source)])
        instructions="Install the pinned packages in requirements.txt in a Python environment, then run program.py.\n"
        toolchain=subprocess.check_output([ROOT/".venv/bin/python","--version"],text=True).strip()
    else:
        run=shlex.join([str(job/"program")])
        command=cpp_command(job,"program")
        instructions="Build on this host:\n"+shlex.join(command)+"\n\nRun:\n"+run+"\n"
        toolchain=subprocess.check_output(["clang++","--version"],text=True).splitlines()[0]
    (job/"README.txt").write_text(instructions+"\nThe application uses algebraic precedence. Clear resets the expression; memory controls are present only when requested.\n")
    context.last_spec=spec.data();context.last_language=language
    comments=("#",) if language=="python" else ("//",)
    lines=sum(bool(line.strip()) and not line.lstrip().startswith(comments) for line in source.read_text().splitlines())
    hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in job.iterdir() if p.suffix in (".py",".cpp",".h",".g4")}
    report={"status":"validated" if valid else "failed","kind":"application","language":language,
        "source":str(source),"job":str(job),"run":run,"toolchain":toolchain,"meaningful_lines":lines,
        "specification":spec.data(),"emitted_specification":spec.data(),"contract":spec.contract(),
        "construction":construction,"solver":solver,"attempts":[validation],
        "structure":{"state_fields":len(app.state),"functions":len(app.functions)+len(app.operators),
                     "widgets":len(app.widgets),"key_bindings":len(app.keys),"component_graph_nodes":len(app.components)},
        "dependencies":{"antlr":"4.13.2","qt":subprocess.check_output(["pkg-config","--modversion","Qt6Widgets"],text=True).strip(),
                        "pyside6":"6.12.0" if language=="python" else None},
        "grammar":json.loads((DEPS/"generated/manifest.json").read_text())["grammar_sha256"],
        "source_sha256":hashes,"request":request.get("question",""),
        "elapsed_ms":round((time.monotonic()-started)*1000,3),
        "preview":validation.get("preview"),"claim":"tested arithmetic and GUI behavior; no unrestricted correctness proof"}
    report["text"]=(f"Generated and validated {language} calculator app: {source}\nRun:\n{run}\n"
                    f"Validation: {validation.get('arithmetic_cases',0)} arithmetic cases and {validation.get('gui_scenarios',0)} GUI scenarios"
                    +(" in normal and sanitizer builds." if language=="cpp" else ".")
                    if valid else f"The application candidate failed validation. Report: {job/'report.json'}")
    (job/"report.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    return report
