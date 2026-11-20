#!/usr/bin/env python3
"""Behavioral acceptance tests for the cross-domain pilot. Runs the delivered artifact in isolation."""
import gzip
import json
import os
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
case = os.environ["DOMAIN_CASE"]
program, work = (Path(value).resolve(strict=True) for value in sys.argv[1:])
checks = {}


def execute(mounts, args):
    base = ["bwrap", "--die-with-parent", "--unshare-all", "--clearenv", "--setenv", "PATH",
            "/usr/bin", "--setenv", "HOME", "/tmp", "--ro-bind", "/usr", "/usr",
            "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib", "/lib64",
            "--symlink", "usr/bin", "/bin", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/in"]
    run = subprocess.run(base + mounts + ["--chdir", "/out", "--"] + args,
                         stdin=subprocess.DEVNULL, capture_output=True, timeout=25)
    if not 0 <= run.returncode < 126 or run.stderr.startswith(b"bwrap:"):
        raise ValueError("abnormal program or sandbox exit")
    return run.returncode


def file_case(label, content, expected, kind):
    dest = work / label
    dest.mkdir()
    path = "/in/source with spaces"
    mounts = ["--ro-bind", str(program), "/program", "--bind", str(dest), "/out"]
    if content is not None:
        src = work / (label + ".input")
        src.write_bytes(content)
        mounts += ["--ro-bind", str(src), path]
    rc = execute(mounts, ["/program", path, "/out/result"])
    out = dest / "result"
    if expected is None:
        if rc == 0:
            raise ValueError("invalid input accepted")
        if case == "build" and content is not None and out.exists():
            raise ValueError("invalid graph published an output")
    elif rc != 0:
        raise ValueError("valid input rejected")
    elif kind == "bytes":
        if out.read_bytes() != expected:
            raise ValueError("incorrect decompressed bytes")
    elif json.dumps(json.loads(out.read_text())) != json.dumps(expected):
        raise ValueError("incorrect JSON value, order, or element type")


def library():
    if not (program.parent / "waitlist.hpp").is_file():
        raise ValueError("requested header missing from delivered project")
    mounts = ["--ro-bind", str(program.parent), "/project", "--ro-bind",
              str(here / "waitlist-test.cpp"), "/in/test.cpp", "--bind", str(work), "/out"]
    if execute(mounts, ["c++", "-std=c++17", "-I/project", "/in/test.cpp", "-o", "/out/client"]):
        raise ValueError("independent C++ caller failed to compile or link")
    if execute(mounts, ["/out/client"]):
        raise ValueError("independent caller observed wrong behavior")


def check(name, fn, *args):
    try:
        fn(*args)
        checks[name] = {"pass_": True}
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        checks[name] = {"pass_": False, "detail": str(error)}


if case == "queue":
    check("compile and exercise standalone header", library)
else:
    if case == "gzip":
        payload = b"ABC\x00\xff\n"
        packed = gzip.compress(payload, mtime=0)
        damaged = bytearray(packed)
        damaged[-8] ^= 1
        rows = [("binary", packed, payload),
                ("concatenated", packed + gzip.compress(b"NEXT", mtime=0), payload + b"NEXT"),
                ("empty-member", gzip.compress(b"", mtime=0), b""),
                ("bad-checksum", bytes(damaged), None)]
        kind = "bytes"
    else:
        rows = [(name, value.encode(), answer) for name, value, answer
                in json.loads((here/"fixtures.json").read_text())[case]]
        kind = "json"
    for name, data, answer in rows + [("missing-input", None, None)]:
        check(name, file_case, name, data, answer, kind)
print(json.dumps({"checks": checks}))
