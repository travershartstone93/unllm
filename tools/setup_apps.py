#!/usr/bin/env python3
"""Pinned ANTLR setup and shared grammar artifacts; Qt is supplied by the host."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
DEPS=ROOT/".deps/apps"


def run(command):
    result=subprocess.run([str(x) for x in command],capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout[-3000:]+result.stderr[-5000:])
    return result.stdout


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--check",action="store_true");args=parser.parse_args()
    lock=json.loads((ROOT/"dependencies-apps.lock.json").read_text())
    archive=DEPS/"archives";archive.mkdir(parents=True,exist_ok=True)
    for name,entry in lock["downloads"].items():
        path=archive/name
        if not path.exists():
            if args.check:raise RuntimeError("Missing application archive: "+name)
            with urllib.request.urlopen(entry["url"],timeout=60) as response:path.write_bytes(response.read())
        if hashlib.sha256(path.read_bytes()).hexdigest()!=entry["sha256"]:
            raise RuntimeError("Application archive checksum mismatch: "+name)
    runtime=DEPS/"native/lib/libantlr4-runtime.a"
    if not runtime.exists() and not args.check:
        source=DEPS/"antlr-source"
        with zipfile.ZipFile(archive/"antlr4-cpp-runtime-4.13.2-source.zip") as content:content.extractall(source)
        run(["cmake","-S",source,"-B",DEPS/"antlr-build","-DCMAKE_BUILD_TYPE=Release",
             "-DANTLR_BUILD_CPP_TESTS=OFF","-DANTLR_BUILD_SHARED=OFF","-DANTLR_BUILD_STATIC=ON",
             "-DDISABLE_WARNINGS=ON","-DCMAKE_INSTALL_PREFIX="+str(DEPS/"native")])
        print("Building local ANTLR C++ runtime",flush=True)
        run(["cmake","--build",DEPS/"antlr-build","--target","install","--parallel","2"])
    if not runtime.exists():raise RuntimeError("ANTLR C++ runtime missing; run tools/setup_apps.py")
    versions=run(["pkg-config","--modversion","Qt6Widgets","Qt6Test"]).splitlines()
    grammar=ROOT/"synthesis/apps/Arithmetic.g4"
    key=hashlib.sha256(grammar.read_bytes()+json.dumps(lock,sort_keys=True).encode()+run(["clang++","--version"]).encode()).hexdigest()
    manifest=DEPS/"generated/manifest.json"
    expected=manifest.exists() and json.loads(manifest.read_text()).get("key")==key
    if not expected and args.check:raise RuntimeError("Grammar artifacts are out of date; run tools/setup_apps.py")
    if not expected:
        output=DEPS/"generated"
        for language,folder in (("Python3","python"),("Cpp","cpp")):
            target=output/folder;target.mkdir(parents=True,exist_ok=True)
            run(["java","-jar",archive/"antlr-4.13.2-complete.jar","-Dlanguage="+language,"-visitor","-no-listener",
                 "-Xexact-output-dir","-o",target,grammar])
        include=DEPS/"native/include/antlr4-runtime"
        sources=list((output/"cpp").glob("*.cpp"))
        for variant,flags in (("normal",["-O2"]),("checked",["-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer"])):
            objects=[]
            for source in sources:
                obj=output/(source.stem+"."+variant+".o")
                run(["clang++","-std=c++20","-DANTLR4CPP_STATIC","-I",include,"-I",output/"cpp",
                     *flags,"-c",source,"-o",obj]);objects.append(obj)
            run(["ar","rcs",output/("expression."+variant+".a"),*objects])
        generated={str(p.relative_to(output)):hashlib.sha256(p.read_bytes()).hexdigest()
                   for folder in ("python","cpp") for p in (output/folder).iterdir() if p.is_file()}
        manifest.write_text(json.dumps({"key":key,"grammar_sha256":hashlib.sha256(grammar.read_bytes()).hexdigest(),
                                      "antlr":"4.13.2","files":generated},indent=2)+"\n")
    else:
        data=json.loads(manifest.read_text())
        for name,sha in data["files"].items():
            if hashlib.sha256((manifest.parent/name).read_bytes()).hexdigest()!=sha:raise RuntimeError("Modified grammar artifact: "+name)
    print(json.dumps({"antlr":"4.13.2","qt_widgets":versions[0],"qt_test":versions[1],
                      "grammar_sha256":hashlib.sha256(grammar.read_bytes()).hexdigest(),"ready":True}),flush=True)


if __name__=="__main__":main()
