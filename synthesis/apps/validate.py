"""Independent arithmetic oracle, state scenarios, Qt Test and isolation."""
from pathlib import Path
import ast
import io
import json
import math
import os
import resource
import shlex
import shutil
import subprocess
import sys
import time
import tokenize
from hypothesis import given,settings,strategies as st

ROOT=Path(__file__).resolve().parents[2]
DEPS=ROOT/".deps/apps"
TESTING=Path(__file__).parent/"testing"
SYMBOLS={"add":"+","subtract":"-","multiply":"*","divide":"/"}


def checked(value):
    if not math.isfinite(value) or abs(value)>1e15:raise ValueError("numeric domain")
    return value


def oracle(text,spec):
    """Python tokenizer/AST evaluation is independent of the ANTLR visitor."""
    if not text or len(text)>1024:raise ValueError("input domain")
    tokens=list(tokenize.generate_tokens(io.StringIO(text).readline))
    meaningful=[t for t in tokens if t.type not in (tokenize.NEWLINE,tokenize.NL,tokenize.ENDMARKER,tokenize.ENCODING)]
    if len(meaningful)+1>256:raise ValueError("tokens")
    depth=0;prefix=0;expecting=True;normalized=[];index=0
    while index<len(meaningful):
        token=meaningful[index]
        if token.type==tokenize.NUMBER:
            word=token.string;end=token.end
            while index+1<len(meaningful) and meaningful[index+1].type==tokenize.NUMBER and meaningful[index+1].start==end:
                index+=1;word+=meaningful[index].string;end=meaningful[index].end
            if any(c not in "0123456789.eE+-" for c in word):raise ValueError("decimal literal")
            normalized.append((tokenize.NUMBER,repr(checked(float(word)))));expecting=False
        elif token.type==tokenize.OP and token.string in ("+","-","*","/","(",")"):
            word=token.string
            if word in ("(",")"):
                if not spec.parentheses:raise ValueError("parentheses")
                depth+=1 if word=="(" else -1
                if depth>32:raise ValueError("depth")
            if word in ("+","-") and expecting:
                prefix+=1
                if prefix>32:raise ValueError("prefix")
            expecting=word!=")"
            normalized.append((tokenize.OP,word))
        else:raise ValueError("unsupported token")
        index+=1
    tree=ast.parse("("+tokenize.untokenize(normalized)+")",mode="eval").body
    def value(node):
        if isinstance(node,ast.Constant) and type(node.value) in (int,float):return checked(float(node.value))
        if isinstance(node,ast.UnaryOp) and isinstance(node.op,(ast.UAdd,ast.USub)):
            number=value(node.operand);return checked(-number if isinstance(node.op,ast.USub) else number)
        if not isinstance(node,ast.BinOp):raise ValueError("unsupported tree")
        op={ast.Add:"add",ast.Sub:"subtract",ast.Mult:"multiply",ast.Div:"divide"}.get(type(node.op))
        if op not in spec.operations:raise ValueError("disabled operation")
        a,b=value(node.left),value(node.right)
        if op=="divide" and b==0:raise ValueError("zero divisor")
        result=a+b if op=="add" else a-b if op=="subtract" else a*b if op=="multiply" else a/b
        return checked(result)
    return value(tree)


def expression_cases():
    fixed=["0","12","2+3*4","(2+3)*4","-2.5+1","1/0","1/(2-2)","0.1+0.2",
           "1e-500","1e15","1e16","1e15/0.01","1+","",")(","1//2","2**3","1_000+1",
           "__import__('os')","open('/etc/passwd')","true","0x10","0001+02","1 2",
           "(" * 33+"1"+")"*33,"-"*33+"1","1+"*150+"1","9"*1025]
    atom=st.one_of(st.integers(-50,50).map(str),st.sampled_from(["0.1","0.2",".5","12.75","-0.25","1e-4"]))
    strategy=st.recursive(atom,lambda children:st.tuples(children,st.sampled_from(["+","-","*","/"]),children).map(
        lambda item:"("+item[0]+item[1]+item[2]+")"),max_leaves=8)
    generated=[]
    @settings(max_examples=60,derandomize=True,database=None,deadline=None)
    @given(strategy)
    def collect(text):generated.append(text)
    collect()
    return list(dict.fromkeys(fixed+generated))


def display(value,spec):return format(0.0 if value==0 else value,"."+str(spec.precision)+"g")


def scenarios(spec):
    op=spec.operations[0];symbol=SYMBOLS[op]
    expected=oracle("12"+symbol+"3",spec)
    rows=[{"name":"buttons","events":[{"button":b} for b in ("digit_1","digit_2",op,"digit_3","evaluate")],
           "expected":display(expected,spec)},
          {"name":"clear","events":[{"button":"digit_7"},{"button":"clear"}],"expected":"0"},
          {"name":"decimal","events":[{"button":b} for b in ("decimal","digit_5",op,"digit_2","evaluate")],
           "expected":display(oracle(".5"+symbol+"2",spec),spec)}]
    if spec.keyboard:
        rows += [{"name":"keyboard","events":[{"text":"7"+symbol+"2"},{"key":"Return"}],
                  "expected":display(oracle("7"+symbol+"2",spec),spec)},
                 {"name":"keyboard_clear","events":[{"text":"123"},{"key":"Escape"}],"expected":"0"},
                 {"name":"fresh_after_equals","events":[{"text":"2"+symbol+"3"},{"key":"Return"},{"text":"4"}],"expected":"4"},
                 {"name":"continue_after_equals","events":[{"text":"2"+symbol+"3"},{"key":"Return"},{"text":symbol+"2"},{"key":"Return"}],
                  "expected":display(oracle("("+str(oracle("2"+symbol+"3",spec))+")"+symbol+"2",
                                          type("Contract",(),{"operations":spec.operations,"parentheses":True})()),spec)},
                 {"name":"malformed_expression","events":[{"text":"1..2"},{"key":"Return"}],"error":True},
                 {"name":"recover_from_malformed","events":[{"text":"1..2"},{"key":"Return"},{"text":"3"}],"expected":"3"},
                 {"name":"leading_zero","events":[{"text":"0005"},{"key":"Return"}],"expected":"5"},
                 {"name":"input_budget_recovery","events":[{"text":"9"*1025},{"key":"Escape"},{"text":"4"}],"expected":"4"}]
        if spec.parentheses:
            rows.append({"name":"parentheses","events":[{"text":"(2"+symbol+"3)"+symbol+"4"},{"key":"Return"}],
                         "expected":display(oracle("(2"+symbol+"3)"+symbol+"4",spec),spec)})
        if "divide" in spec.operations:
            rows += [{"name":"zero_divisor","events":[{"text":"8/0"},{"key":"Return"}],"error":True},
                     {"name":"recover_after_error","events":[{"text":"8/0"},{"key":"Return"},{"text":"6"}],"expected":"6"}]
        disabled=next((op for op in SYMBOLS if op not in spec.operations),None)
        if disabled:rows.append({"name":"disabled_keyboard_operator","events":[{"text":"2"+SYMBOLS[disabled]+"3"},{"key":"Return"}],"error":True})
    else:
        rows.append({"name":"keyboard_disabled","events":[{"text":"123"},{"key":"Return"}],"expected":"0"})
    if spec.backspace:
        rows.append({"name":"backspace","events":[{"button":b} for b in ("digit_1","digit_2","backspace")],"expected":"1"})
    if spec.memory:
        rows += [{"name":"memory_accumulate","events":[{"button":b} for b in
                  ("digit_1","digit_2","memory_add","clear","digit_3","memory_subtract","memory_recall")],"expected":"9","memory":9},
                 {"name":"memory_clear","events":[{"button":b} for b in
                  ("digit_5","memory_add","memory_clear","memory_recall")],"expected":"0","memory":0}]
        if spec.keyboard:
            rows.append({"name":"memory_overflow_is_atomic","events":[{"text":"1000000000000000"},{"button":"memory_add"},
                          {"button":"memory_add"},{"button":"memory_recall"}],
                         "expected":display(1e15,spec),"memory":1e15})
    return rows


def sandbox(job,command,payload,output,sanitized=False,timeout=20,python=False):
    def limits():
        resource.setrlimit(resource.RLIMIT_CPU,(15,15))
        resource.setrlimit(resource.RLIMIT_FSIZE,(8*1024*1024,8*1024*1024))
        resource.setrlimit(resource.RLIMIT_NOFILE,(128,128))
        if not sanitized:resource.setrlimit(resource.RLIMIT_AS,(1536*1024*1024,1536*1024*1024))
    args=["bwrap","--unshare-all","--die-with-parent","--clearenv","--ro-bind","/usr","/usr",
          "--symlink","usr/lib","/lib","--symlink","usr/lib","/lib64","--proc","/proc","--dev","/dev",
          "--tmpfs","/tmp","--perms","0700","--dir","/tmp/runtime","--ro-bind",str(Path(job).resolve()),"/job",
          "--bind",str(Path(output).resolve()),"/output","--chdir","/job",
          "--setenv","PATH","/usr/bin","--setenv","LANG","C.UTF-8","--setenv","LC_ALL","C.UTF-8",
          "--setenv","XDG_RUNTIME_DIR","/tmp/runtime","--setenv","QT_QPA_PLATFORM","offscreen",
          "--setenv","ASAN_OPTIONS","detect_leaks=0","--setenv","UBSAN_OPTIONS","halt_on_error=1"]
    if Path("/etc/fonts").exists():args+=["--ro-bind","/etc/fonts","/etc/fonts"]
    if python:
        base=Path(sys.base_prefix)
        args+=["--ro-bind",str(base),"/runtime/python","--ro-bind",str(ROOT/".venv"),"/runtime/venv",
               "--setenv","PYTHONHOME","/runtime/python",
               "--setenv","PYTHONPATH","/runtime/venv/lib/python3.12/site-packages"]
    return subprocess.run([*args,*command],input=json.dumps(payload),text=True,capture_output=True,
                          timeout=timeout,preexec_fn=limits,start_new_session=True)


def cpp_command(job,filename,sanitized=False,driver=False):
    flags=shlex.split(subprocess.check_output(["pkg-config","--cflags","--libs","Qt6Widgets",*(["Qt6Test"] if driver else [])],text=True))
    return ["clang++","-std=c++20","-Wall","-Wextra","-DANTLR4CPP_STATIC",
        *(["-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer"] if sanitized else ["-O2"]),
        str(job/("gui_driver.cpp" if driver else "program.cpp")),
        "-I"+str(job),"-I"+str(DEPS/"native/include/antlr4-runtime"),
        str(DEPS/("generated/expression.checked.a" if sanitized else "generated/expression.normal.a")),
        str(DEPS/"native/lib/libantlr4-runtime.a"),*flags,"-o",str(job/filename)]


def validate(spec,app,job,language,deadline):
    job=Path(job).resolve();compilations=[];commands=[]
    if language=="python":
        shutil.copyfile(TESTING/"python_driver.py",job/"gui_driver.py")
        commands=[(["/runtime/python/bin/python3.12","/job/gui_driver.py"],False,True)]
    else:
        shutil.copyfile(TESTING/"cpp_driver.cpp",job/"gui_driver.cpp")
        for filename,sanitized,driver in (("program",False,False),("gui_driver",False,True),("gui_driver.checked",True,True)):
            command=cpp_command(job,filename,sanitized,driver)
            result=subprocess.run(command,text=True,capture_output=True,timeout=max(.1,min(50,deadline-time.monotonic())))
            compilations.append({"command":command,"returncode":result.returncode,"diagnostics":result.stderr[-6000:]})
            if result.returncode:return {"valid":False,"stage":"compile","compilations":compilations}
            if driver:commands.append((["/job/"+filename],sanitized,False))
    expressions=expression_cases();gui=scenarios(spec)
    request={"expressions":expressions,"gui":gui,"preview":True}
    expected=[]
    for text in expressions:
        try:expected.append(oracle(text,spec))
        except (ValueError,SyntaxError,tokenize.TokenError,ZeroDivisionError,OverflowError):expected.append(None)
    checks=0;observations=[]
    output=job/"test-output";output.mkdir(exist_ok=True)
    expected_buttons=sorted(w.identifier for w in app.widgets if w.kind=="button")
    for command,sanitized,python in commands:
        result=sandbox(job,command,request,output,sanitized,max(.1,min(25,deadline-time.monotonic())),python)
        if result.returncode:return {"valid":False,"stage":"execution","returncode":result.returncode,"stderr":result.stderr[-8000:],
                                    "stdout":result.stdout[-2000:],"compilations":compilations}
        response=json.loads(result.stdout);observations.append(response)
        if len(response["expressions"])!=len(expected) or len(response["gui"])!=len(gui):raise ValueError("Incomplete app validation response")
        for text,wanted,actual in zip(expressions,expected,response["expressions"]):
            valid=(bool(actual["error"]) and actual["value"] is None) if wanted is None else (
                not actual["error"] and math.isfinite(actual["value"]) and math.isclose(actual["value"],wanted,rel_tol=1e-11,abs_tol=1e-12))
            checks+=1
            if not valid:return {"valid":False,"stage":"arithmetic","input":text,"expected":wanted,"actual":actual,"compilations":compilations}
        for scenario,actual in zip(gui,response["gui"]):
            valid=actual["display"]==scenario["expected"] if "expected" in scenario else actual["display"].startswith("Error: ")
            valid=valid and actual["buttons"]==expected_buttons and actual["title"]==spec.title
            if "memory" in scenario:
                observed=actual.get("memory")
                if observed is None:observed=float(actual["memory_text"].removeprefix("Memory: "))
                valid=valid and observed==scenario["memory"]
            checks+=1
            if not valid:return {"valid":False,"stage":"gui","scenario":scenario,"actual":actual,"compilations":compilations}
    return {"valid":True,"cases":checks,"arithmetic_cases":len(expressions),"valid_arithmetic":sum(x is not None for x in expected),
            "gui_scenarios":len(gui),"sanitizers":language=="cpp","compilations":compilations,
            "observations":observations,"method":"independent Python-tokenizer/AST oracle, Hypothesis expressions, Qt Test mouse and keyboard events",
            "sandbox":"bubblewrap; no network or source/home mounts; read-only source job; isolated output directory",
            "preview":str(output/"preview.png"),"proof":"finite executable tests; no unrestricted correctness proof"}
