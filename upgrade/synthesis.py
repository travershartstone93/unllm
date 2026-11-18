"""Bounded construction -> emit -> execute -> check -> repair job."""
from __future__ import annotations
from pathlib import Path
import ast
from dataclasses import replace
import copy
import json
import os
import subprocess
import time
import uuid
from synthesis.spec import Program
from synthesis.catalog import plan
from synthesis.solver import synthesize_expression
from synthesis.backends import python,cpp
from synthesis.language import interpret
from synthesis.validate import validate
from synthesis.apps.spec import ApplicationSpec
from synthesis.behaviour.spec import BehaviourSpec
from .model import Unsupported,digest

ROOT=Path(__file__).resolve().parents[1]


def synthesize(request,context,engine):
    started=time.monotonic()
    if request.get("_progress"):
        request["_progress"]({"event":"synthesis_started"})
    deadline=started+min(float(os.environ.get("UNLLM_SYNTH_SECONDS","180")),180)
    language=request.get("language") or os.environ.get("UNLLM_CODE_LANGUAGE") or "python"
    language={"c++":"cpp","py":"python"}.get(language.casefold(),language.casefold())
    if language not in ("python","cpp"):raise Unsupported("Code generation currently targets Python and C++.")
    if "spec" in request:
        raw=request["spec"]
        if any(k in raw for k in ("state","views","actions")) or raw.get("schema")==2 or ("program" in raw and raw["program"]!="calculator"):
            original=BehaviourSpec.from_dict(raw)
        else:
            original=ApplicationSpec.from_dict(raw) if "program" in raw else Program.from_dict(raw)
    else:
        meaning=request.get("semantic_meaning")
        if meaning is None:
            meaning=engine.interpret(request.get("question",""),context,"synthesis",programming=True)
        if meaning.act!="synthesis":raise Unsupported("Use a programming request or supply a typed JSON specification.")
        original=interpret(meaning)
        language_words={e.get("carg","").casefold() for e in meaning.semantics if e.get("carg")}
        language_words|={e["predicate"].split("_")[1].casefold() for e in meaning.semantics
                         if e["predicate"].startswith("_") and len(e["predicate"].split("_"))>2}
        if not request.get("language") and ("c++" in language_words or "cpp" in language_words):language="cpp"
    if isinstance(original,BehaviourSpec):
        from synthesis.behaviour.build import build
        return build(original,language,request,context,engine,started,deadline)
    if isinstance(original,ApplicationSpec):
        from synthesis.apps.build import build
        return build(original,language,request,context,engine,started,deadline)
    construction=plan(original,engine.native)
    solved=copy.deepcopy(original)
    solver_reports=[]
    for step in solved.steps:
        if step.expr:
            step.expr,report=synthesize_expression(step.expr,predicate=step.op=="filter",
                timeout_ms=max(100,min(3000,int((deadline-time.monotonic())*1000))))
            solver_reports.append(report)
    job_root=Path(os.environ.get("UNLLM_ARTIFACTS",ROOT/".artifacts"))
    job=job_root/("job-"+uuid.uuid4().hex[:12])
    job.mkdir(parents=True,exist_ok=False)
    source=job/("program.py" if language=="python" else "program.cpp")
    attempts=[]
    for attempt in range(2):
        source.write_text(python.emit(solved) if language=="python" else cpp.emit(solved))
        if language=="cpp":
            subprocess.run(["clang-format","-i",str(source)],check=True,timeout=5)
        validation=validate(original,job,language,deadline)
        attempts.append(validation)
        if validation["valid"]:break
        if attempt==0 and validation.get("stage")=="execution" and any(s.expr for s in original.steps):
            # Real-equivalent terms can differ in floating-point evaluation
            # order. Repair the typed scalar nodes using the specification's
            # order and regenerate both control flow and source, without
            # patching the emitted text.
            solved=copy.deepcopy(original)
            attempts[-1]["repair"]="preserve specified scalar evaluation order"
            continue
        break
    valid=bool(attempts[-1]["valid"])
    context.last_spec=original.data()
    context.last_language=language
    comments=("#",) if language=="python" else ("//",)
    lines=sum(bool(line.strip()) and not line.lstrip().startswith(comments)
              for line in source.read_text().splitlines())
    structure={"pipeline_operations":len(construction["operations"]),"scalar_holes":len(solver_reports)}
    if language=="python":
        nodes=list(ast.walk(ast.parse(source.read_text())))
        structure.update(functions=sum(isinstance(n,ast.FunctionDef) for n in nodes),
                         loops=sum(isinstance(n,(ast.For,ast.While,ast.comprehension)) for n in nodes),
                         branches=sum(isinstance(n,(ast.If,ast.IfExp)) for n in nodes))
    run=(f"python3 {source}" if language=="python" else
         f"clang++ -std=c++20 -O2 {source} -o {job/'program'}\n{job/'program'}")
    report={"status":"validated" if valid else "failed","language":language,
        "source":str(source),"job":str(job),"specification":original.data(),
        "emitted_specification":solved.data(),"construction":construction,"solver":solver_reports,
        "attempts":attempts,"meaningful_lines":lines,"structure":structure,"run":run,
        "request":request.get("question",""),"elapsed_ms":round((time.monotonic()-started)*1000,3),
        "toolchain":subprocess.check_output(["/usr/bin/python3","--version"] if language=="python" else
                                          ["clang++","--version"],text=True).splitlines()[0],
        "claim":"executable behavior validated on the reported cases; no unrestricted correctness proof"}
    (job/"report.json").write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n")
    text=(f"Generated and validated {language} program ({lines} meaningful lines): {source}\n"
          f"Run:\n{run}\nValidation: {attempts[-1].get('cases',0)} isolated executable checks."
          if valid else f"The generated candidate failed validation. Report: {job/'report.json'}")
    return {**report,"text":text}
