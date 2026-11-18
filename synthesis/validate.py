"""Independent executable semantics, generated cases, and isolated execution."""
from __future__ import annotations
from collections import Counter, defaultdict
from pathlib import Path
import math
import os
import resource
import shutil
import subprocess
import time
from hypothesis import given, settings, strategies as st


def number(value):
    if not math.isfinite(value) or abs(value)>1e15:raise ValueError("numeric domain")
    return value


def reference(program,text):
    """Reference follows the contract, independently of either source emitter."""
    if not text:
        rows=[]
    else:
        rows=text.split("\n")
        if text.endswith("\n"):rows.pop()
    if len(rows)>100000:raise ValueError("row budget")
    parsed=[] if program.input!="records" else defaultdict(list)
    for index,raw in enumerate(rows,1):
        try:
            line=raw[:-1] if raw.endswith("\r") else raw
            if index>100000 or len(line)>16384 or any((ord(c)<32 and c!="\t") or ord(c)>=127 for c in line):
                raise ValueError("text domain")
            if program.input=="lines":
                parsed.append(line)
                continue
            if not line.strip():continue
            group=None
            if program.input=="records":
                parts=line.split("\t")
                if len(parts)!=2 or not parts[0].strip():raise ValueError("record shape")
                group,line=parts[0].strip(),parts[1]
            if not line.strip() or any(c in line for c in "_xXpP"):raise ValueError("decimal input")
            value=number(float(line))
            if group is None:parsed.append(value)
            else:parsed[group].append(value)
        except ValueError:
            if program.invalid!="skip":raise
    def transform(values):
        values=list(values)
        for step in program.steps:
            if step.op=="filter":values=list(filter(lambda x:step.expr.evaluate(x),values))
            elif step.op=="map":values=[number(step.expr.evaluate(x)) for x in values]
            elif step.op=="unique":
                unique=[]
                for value in values:
                    if value not in unique:unique.append(value)
                values=unique
            elif step.op=="sort":values.sort(reverse=step.descending)
            elif step.op=="reverse":values=values[::-1]
            elif step.op=="lowercase":values=list(map(str.lower,values))
            elif step.op=="strip":values=list(map(str.strip,values))
            elif step.op=="words":values=sum((line.split() for line in values),[])
        return values
    def render(values):
        op=program.output
        if op=="values":return [[x] for x in values]
        if op=="count":return [[len(values)]]
        if op=="frequencies":
            if any("\t" in x for x in values):raise ValueError("frequency key")
            return [["value","count"],*[[k,v] for k,v in sorted(Counter(values).items())]]
        if not values:
            if op=="sum":return [[0]]
            raise ValueError("empty numeric aggregate")
        total=0.0
        for value in values:total+=value
        if op=="sum":return [[total]]
        if op=="mean":return [[number(total/len(values))]]
        if op=="min":return [[min(values)]]
        if op=="max":return [[max(values)]]
        return [[len(values),total,min(values),max(values),number(total/len(values))]]
    if program.input=="records":
        output=[["key","count","sum","min","max","mean"]] if program.output=="stats" else []
        for group,values in sorted(parsed.items()):
            values=transform(values)
            if values:output.extend([[group,*row] for row in render(values)])
    else:
        output=render(transform(parsed))
        if program.output=="stats":output.insert(0,["count","sum","min","max","mean"])
    for row in output:
        for value in row:
            if isinstance(value,(int,float)):number(value)
    return output


def cases(program):
    if program.input=="numbers":
        fixed=["","0\n","-2\n0\n4.5\n4.5\n","0.0001\n0.000001\n-0.0001\n",
               "1\nbad\n2\n","nan\n","1_0\n","1e-500\n",
               "1000000000000000\n1000000000000000\n"]
        strategy=st.lists(st.integers(-1000,1000),max_size=20).map(
            lambda xs:"".join(str(x)+"\n" for x in xs))
    elif program.input=="records":
        fixed=["","A\t1\nA\t2\nB\t-3\n","A\tbad\nB\t2\n","bad\n","a\t0\n",
               " a \t0.0001\na\t-0.0001\n"]
        strategy=st.lists(st.tuples(st.sampled_from(["a","b","c"]),
            st.integers(-100,100)),max_size=20).map(
                lambda xs:"".join(k+"\t"+str(v)+"\n" for k,v in xs))
    else:
        fixed=["","Hello\nhello\nWorld\n"," a \na\nb\n","\n \n","a\tb\n",
               "one two\nTwo three\n","a\0b\n","caf\u00e9\n"]
        strategy=st.lists(st.text(alphabet="abcABC 012",max_size=15),max_size=12).map(
            lambda xs:"".join(x+"\n" for x in xs))
    generated=[]
    @settings(max_examples=12,derandomize=True,database=None,deadline=None)
    @given(strategy)
    def collect(text):generated.append(text)
    collect()
    return list(dict.fromkeys(fixed+generated))


def limits(sanitized=False):
    def apply():
        resource.setrlimit(resource.RLIMIT_CPU,(3,3))
        resource.setrlimit(resource.RLIMIT_FSIZE,(2*1024*1024,2*1024*1024))
        resource.setrlimit(resource.RLIMIT_NOFILE,(64,64))
        # ASan reserves a very large virtual shadow address range. Its input,
        # output and CPU remain bounded, but an address-space limit would stop
        # the sanitizer before it can check the candidate.
        if not sanitized:
            resource.setrlimit(resource.RLIMIT_AS,(512*1024*1024,512*1024*1024))
    return apply


def run_isolated(job: Path,command:list[str],text:str,sanitized=False,timeout=5):
    if not shutil.which("bwrap"):
        raise RuntimeError("bubblewrap is required to execute synthesized candidates")
    args=["bwrap","--unshare-all","--die-with-parent","--clearenv",
          "--ro-bind","/usr","/usr","--symlink","usr/lib","/lib",
          "--symlink","usr/lib","/lib64","--proc","/proc","--dev","/dev",
          "--tmpfs","/tmp","--ro-bind",str(job.resolve()),"/job","--chdir","/job",
          "--setenv","PATH","/usr/bin","--setenv","LANG","C.UTF-8",
          "--setenv","ASAN_OPTIONS","detect_leaks=0",
          "--setenv","UBSAN_OPTIONS","halt_on_error=1",*command]
    return subprocess.run(args,input=text,text=True,capture_output=True,timeout=timeout,
                          preexec_fn=limits(sanitized),start_new_session=True)


def matches(expected,output):
    lines=output.removesuffix("\n").split("\n") if output else []
    if len(lines)!=len(expected):return False
    for gold,line in zip(expected,lines):
        if len(gold)==1 and isinstance(gold[0],str):
            if gold[0]!=line:return False
            continue
        values=line.split("\t")
        if len(values)!=len(gold):return False
        for wanted,actual in zip(gold,values):
            if type(wanted) is int:
                try:
                    if int(actual)!=wanted:return False
                except ValueError:return False
            elif isinstance(wanted,float):
                try:
                    value=float(actual)
                    if not math.isfinite(value) or not math.isclose(value,wanted,rel_tol=1e-10,abs_tol=1e-12):
                        return False
                except ValueError:return False
            elif str(wanted)!=actual:return False
    return True


def validate(program,job,language,deadline):
    job=Path(job).resolve()
    compilation=[]
    commands=[]
    if language=="python":
        source=(job/"program.py").read_text()
        compile(source,str(job/"program.py"),"exec")
        commands=[(["/usr/bin/python3","/job/program.py"],False)]
    else:
        for filename,flags in [("program",["-O2"]),("program.checked",
                ["-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer"])]:
            args=["clang++","-std=c++20","-Wall","-Wextra",*flags,
                  str(job/"program.cpp"),"-o",str(job/filename)]
            result=subprocess.run(args,cwd=job,capture_output=True,text=True,
                                  timeout=max(1,min(30,deadline-time.monotonic())))
            compilation.append({"command":args,"returncode":result.returncode,"diagnostics":result.stderr[-4000:]})
            if result.returncode:
                return {"valid":False,"stage":"compile","compilation":compilation}
            commands.append((["/job/"+filename],filename.endswith("checked")))
    checked=0
    for text in cases(program):
        try: expected=reference(program,text); valid_input=True
        except (ValueError,OverflowError):expected=[];valid_input=False
        for command,sanitized in commands:
            if time.monotonic()>=deadline:
                return {"valid":False,"stage":"budget","cases":checked,"compilation":compilation}
            try:
                result=run_isolated(job,command,text,sanitized,
                                    timeout=max(0.1,min(5,deadline-time.monotonic())))
                passed=(result.returncode==0 and matches(expected,result.stdout)) if valid_input else (
                    result.returncode==2 and not result.stdout and result.stderr.startswith("error: "))
            except subprocess.TimeoutExpired:
                return {"valid":False,"stage":"execution_timeout","input":text,"cases":checked}
            checked+=1
            if not passed:
                return {"valid":False,"stage":"execution","input":text,"expected":expected,
                        "expected_valid":valid_input,"returncode":result.returncode,
                        "stdout":result.stdout,"stderr":result.stderr,"cases":checked,
                        "compilation":compilation}
    return {"valid":True,"cases":checked,"compilation":compilation,
            "method":"independent contract interpreter, fixed edge cases and Hypothesis inputs",
            "sandbox":"bubblewrap; read-only job; no network; CPU/output limits",
            "sanitizers":language=="cpp","proof":"tested executable behavior, not unrestricted correctness"}
