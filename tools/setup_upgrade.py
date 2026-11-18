#!/usr/bin/env python3
"""Reproduce the checkout-local Linux x86_64 toolchain without root installs."""
from pathlib import Path
import argparse
import bz2
import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import urllib.request

ROOT=Path(__file__).resolve().parents[1]


def run(args,**kwargs):
    return subprocess.run([str(a) for a in args],check=True,**kwargs)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--build",action="store_true")
    parser.add_argument("--check",action="store_true")
    args=parser.parse_args()
    if platform.system()!="Linux" or platform.machine()!="x86_64":
        parser.error("This locked native bundle targets Linux x86_64.")
    deps=ROOT/".deps";archives=deps/"archives";native=deps/"native";binary=deps/"bin"
    for folder in (archives,native,binary):folder.mkdir(parents=True,exist_ok=True)
    lock=json.loads((ROOT/"dependencies-upgrade.lock.json").read_text())
    downloaded={}
    for name,item in lock["downloads"].items():
        candidates=[archives/name,archives/item["url"].rsplit("/",1)[-1]]
        target=next((p for p in candidates if p.exists()),candidates[0])
        if not target.exists():
            if args.check:raise RuntimeError("Missing locked archive: "+name)
            request=urllib.request.Request(item["url"],headers={"User-Agent":"unllm-upgrade-setup/1"})
            temporary=target.with_name(target.name+".part")
            with urllib.request.urlopen(request,timeout=60) as src,temporary.open("wb") as dst:
                shutil.copyfileobj(src,dst)
            temporary.replace(target)
        sha=hashlib.sha256(target.read_bytes()).hexdigest()
        if sha!=item["sha256"]:raise RuntimeError("Checksum mismatch: "+str(target))
        downloaded[name]=target
        print("Verified",name,flush=True)
    if not args.check:
        for name,target in downloaded.items():
            if name in ("souffle.deb","libncurses6"):
                members=subprocess.check_output(["ar","t",str(target)],text=True).splitlines()
                member=next(m for m in members if m.startswith("data.tar"))
                data=deps/(name+"-"+member)
                data.write_bytes(subprocess.check_output(["ar","p",str(target),member]))
                run(["tar","-xf",data,"-C",native])
            elif name=="swipl":
                run(["tar","--zstd","-xf",target,"-C",native])
            elif name=="ace.tar.gz":
                run(["tar","-xf",target,"-C",deps])
            elif name=="erg.dat.bz2":
                with bz2.open(target,"rb") as src,(deps/"erg.dat").open("wb") as dst:
                    shutil.copyfileobj(src,dst)
        libpath=":".join(str(deps/p) for p in ("native/usr/lib/swipl/lib/x86_64-linux",
                                             "native/usr/lib/x86_64-linux-gnu","native/usr/lib"))
        for tool,real,options in [("swipl",native/"usr/bin/swipl",[]),
                                 ("souffle",native/"usr/bin/souffle",[]),
                                 ("ace",deps/"ace-0.9.34/ace",["--maxent=/dev/null"])]:
            script="#!/bin/sh\nexport LD_LIBRARY_PATH="+shlex.quote(libpath)+"\n"
            if tool=="swipl":script+="export SWI_HOME_DIR="+shlex.quote(str(native/"usr/lib/swipl"))+"\n"
            script+="exec "+shlex.quote(str(real))+"".join(" "+shlex.quote(o) for o in options)+' "$@"\n'
            path=binary/tool;path.write_text(script);path.chmod(0o755)
        uv=shutil.which("uv")
        if not uv:raise RuntimeError("Install uv or create .venv with Python 3.12 and install requirements-upgrade.txt.")
        if not (ROOT/".venv/bin/python").exists():run([uv,"venv",ROOT/".venv","--python","3.12"])
        run([uv,"pip","install","--python",ROOT/".venv/bin/python","-r",ROOT/"requirements-upgrade.txt"])
    for tool,flag in (("swipl","--version"),("souffle","--version"),("ace","-V")):
        result=subprocess.check_output([str(binary/tool),flag],text=True,stderr=subprocess.STDOUT)
        lines=[line for line in result.splitlines() if line.strip() and set(line.strip())!={"-"}]
        print(lines[0],flush=True)
    expected=dict(line.strip().split("==",1) for line in (ROOT/"requirements-upgrade.txt").read_text().splitlines()
                  if line.strip() and not line.lstrip().startswith("#"))
    run([ROOT/".venv/bin/python","-c",
         "import json,sys; from importlib.metadata import version; wanted=json.loads(sys.argv[1]); "
         "actual={p:version(p) for p in wanted}; print(json.dumps(actual)); "
         "assert actual==wanted, 'Python dependency versions differ from requirements-upgrade.txt'",json.dumps(expected)])
    run([ROOT/".venv/bin/python",ROOT/"tools/setup_apps.py",*(["--check"] if args.check else [])])
    if args.build:
        command=["cmake","-S",ROOT,"-B",ROOT/"build","-DUNLLM_SYMBOLIC=ON","-DCMAKE_BUILD_TYPE=Release"]
        ort=Path.home()/".local/lib/unllm-ort"
        if (ort/"libonnxruntime.so.1").exists():command+=["-DUNLLM_ORT_LIBDIR="+str(ort)]
        run(command);run(["cmake","--build",ROOT/"build","--parallel","2"])


if __name__=="__main__":main()
