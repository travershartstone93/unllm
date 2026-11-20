#!/usr/bin/env python3
"""Disable one helper at a time in a copy, rebuild, and run the original external evaluator."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

run, evaluator, out = (Path(x).resolve() for x in sys.argv[1:])
out.mkdir()
anchors = {
    "keyed_latch.hpp": "if (event == LatchEvent::trigger)",
    "tab_rows.hpp": "std::ifstream in(path);",
    "fifo_pairs.hpp": "auto& queues = by_key_[key];",
    "dependency_waves.hpp": "std::map<std::string, std::size_t> remaining;",
    "gzip_stream.hpp": "std::ifstream in(input, std::ios::binary);",
    "unique_fifo.hpp": "if (std::find(entries_.begin()",
    "json_sequence.hpp": "std::unique_ptr<json_object,",
}
packages = (run/"project/packages.txt").read_text().splitlines()
flags = subprocess.check_output(["pkg-config","--cflags","--libs",*packages],text=True).split() if packages else []
helpers = json.loads((run/"components/helpers.json").read_text())
outcome = json.loads((run/"result.json").read_text())
stage = outcome["completed_at"]
if stage == "requires_verification" and outcome["checks_passed"]:
    stage = outcome["candidate_stage"]
check_stage = {"deterministic": "initial", "compiler_fixit": "fixit", "model_edit": "edit"}[stage]
original = json.loads((run/f"{check_stage}-checks.json").read_text())["checks"]
results = {}
for helper in helpers:
    dest = out/helper["id"]
    shutil.copytree(run/"bricks", dest/"bricks")
    shutil.copytree(run/"project", dest/"project")
    header = dest/"bricks"/helper["header"]
    text = header.read_text()
    anchor = anchors[header.name]
    if text.count(anchor) != 1:
        raise RuntimeError("Unexpected helper revision")
    header.write_text("#include <stdexcept>\n" + text.replace(anchor, 'throw std::runtime_error("disabled helper");\n        '+anchor))
    with (dest/"build.log").open("w") as log:
        subprocess.run(["c++","-std=c++17","-O2","-I"+str(dest/"bricks"),str(dest/"project/converter.cpp"),
                        "-o",str(dest/"project/converter"),*flags],stdout=log,stderr=log,check=True)
    (dest/"check").mkdir()
    proc = subprocess.run([evaluator,dest/"project/converter",dest/"check"],capture_output=True,text=True,
                          env=dict(os.environ, DOMAIN_CASE=os.environ.get("DOMAIN_CASE", run.name)),check=True)
    (dest/"checks.json").write_text(proc.stdout)
    checks = json.loads(proc.stdout)["checks"]
    assert checks.keys() == original.keys()
    results[helper["id"]] = [name for name in checks if original[name]["pass_"] and not checks[name]["pass_"]]
(out/"use.json").write_text(json.dumps(results,indent=2)+"\n")
