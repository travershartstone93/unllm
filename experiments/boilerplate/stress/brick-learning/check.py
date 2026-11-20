#!/usr/bin/env python3
"""Task-level behavior in an isolated filesystem. Expected results are independent of GLib."""
import json
import os
import subprocess
import sys
from pathlib import Path

program, directory = [Path(p).resolve() for p in sys.argv[1:]]
suite = os.environ["LEARNING_CASE"]
if suite == "create":
    cases = [
        ("order", b"# bookmarks\r\nhttps://a/one\r\nfile:///b\nhttps://a/one\nhttps://z/",
         ["https://z/","https://a/one","file:///b","https://a/one"]),
        ("comments", b"# first\n\n# second\r\n", []),
        ("escaped", b"file:///a%20b\nhttps://b/#fragment", ["https://b/#fragment","file:///a%20b"]),
    ]
elif suite == "transfer":
    cases = [
        ("lengths", b"file:///x\r\n# ignore this long comment\r\nhttps://a/b\nfile:///x", [9,11,9]),
        ("escaped", b"\nhttps://x/%20\nfile:///ab#c", [13,12]),
        ("empty", b"", []),
    ]
else:
    raise ValueError(suite)
cases += [("missing", None, None), ("embedded-nul", b"file:///a\0bad", None)]
checks = {}
for name, data, expected in cases:
    folder = directory / name
    folder.mkdir()
    if data is not None:
        (folder / "source").write_bytes(data)
    code = subprocess.run(["bwrap","--die-with-parent","--unshare-all","--ro-bind","/usr","/usr",
        "--symlink","usr/lib","/lib","--symlink","usr/lib","/lib64","--ro-bind",str(program),"/prog",
        "--bind",str(folder),"/work","--chdir","/work","/prog","source","dest"], timeout=10).returncode
    out = folder / "dest"
    passed = 0 < code < 126 and not out.exists() if expected is None else code == 0 and out.is_file() and json.loads(out.read_text()) == expected
    checks[name] = {"pass_":passed}
print(json.dumps({"checks":checks}))
