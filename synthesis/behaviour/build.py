"""One construction and validation path for arbitrary behaviour specifications."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import time
import uuid
from .planner import plan
from .construct import construct,data
from . import emit
from .validate import validate

ROOT=Path(__file__).resolve().parents[2]


def build(spec,language,request,context,engine,started,deadline):
    construction=plan(spec,engine.native,language)
    ir,solver_reports=construct(spec,construction)
    job=Path(os.environ.get("UNLLM_ARTIFACTS",ROOT/".artifacts"))/("job-"+uuid.uuid4().hex[:12])
    job.mkdir(parents=True,exist_ok=False);job=job.resolve()
    source=job/("program.py" if language=="python" else "program.cpp")
    source.write_text(emit.python(spec,ir) if language=="python" else emit.cpp(spec,ir))
    if language=="cpp":subprocess.run(["clang-format","-i",str(source)],check=True,timeout=5)
    else:(job/"requirements.txt").write_text("PySide6-Essentials==6.12.0\nshiboken6==6.12.0\n")
    (job/"specification.json").write_text(json.dumps(spec.data(),indent=2,ensure_ascii=False)+"\n")
    (job/"application.ir.json").write_text(json.dumps(data(ir),indent=2,ensure_ascii=False)+"\n")
    try:validation=validate(spec,job,language,deadline)
    except subprocess.TimeoutExpired:validation={"valid":False,"stage":"budget","message":"Construction or validation exceeded its budget."}
    except (ValueError,RuntimeError,OSError) as error:validation={"valid":False,"stage":"validation","message":str(error)}
    run=shlex.join([str(ROOT/".venv/bin/python"),str(source)]) if language=="python" else shlex.join([str(job/"program")])
    status="validated" if validation["valid"] else "failed"
    toolchain=subprocess.check_output([ROOT/".venv/bin/python","--version"] if language=="python" else ["clang++","--version"],text=True).splitlines()[0]
    context.last_spec=spec.data();context.last_language=language
    report={"status":status,"kind":"behaviour_program","language":language,"source":str(source),"job":str(job),
            "run":run,"specification":spec.data(),"contract":spec.contract(),"construction":construction,
            "solver":solver_reports,"attempts":[validation],"toolchain":toolchain,
            "source_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in job.iterdir() if p.suffix in (".py",".cpp",".h")},
            "meaningful_lines":sum(bool(s.strip()) and not s.lstrip().startswith(("#","//")) for s in source.read_text().splitlines()),
            "structure":{"state_fields":len(ir["state"]),"functions":len(ir["actions"]),
                         "widgets":len(ir["views"])+sum(a.get("visible",True) for a in spec.actions),"primitive_providers":len(construction["providers"])},
            "preview":validation.get("preview"),"elapsed_ms":round((time.monotonic()-started)*1000,3),
            "request":request.get("question",""),"claim":"contract-directed construction with finite executable validation"}
    report["text"]=(f"Generated and validated {language} program from behaviour contracts: {source}\nRun:\n{run}\n"
                    f"Validation: {validation.get('gui_scenarios',0)} scenarios"
                    +(" in normal and sanitizer builds." if language=="cpp" else ".")
                    if status=="validated" else f"The candidate failed validation. Report: {job/'report.json'}")
    (job/"report.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    (job/"README.txt").write_text("Run:\n"+run+"\n\n"+json.dumps(spec.contract(),indent=2)+"\n")
    return report
