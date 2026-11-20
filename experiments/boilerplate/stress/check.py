#!/usr/bin/env python3
"""Independent acceptance runner. STRESS_CASE chooses the oracle, never the implementation."""
import json
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

import cairo
from PIL import Image, ImageChops, ImageOps, ImageStat

case = os.environ["STRESS_CASE"]
program = Path(sys.argv[1]).resolve(strict=True)
work = Path(sys.argv[2]).resolve(strict=True)
root = Path(os.environ.get("CORPUS_ROOT", "/run/media/whisper/UNLLM_CORPUS/unllm"))
fixtures = root / "experiments/boilerplate-B16/fixtures"
checks = {}


def invoke(source, dest, *outputs):
    dest.mkdir()
    args = ["bwrap", "--die-with-parent", "--unshare-all", "--clearenv",
            "--setenv", "PATH", "/usr/bin", "--setenv", "LANG", "C.UTF-8",
            "--setenv", "HOME", "/tmp", "--ro-bind", "/usr", "/usr",
            "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib", "/lib64",
            "--symlink", "usr/bin", "/bin", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/in", "--ro-bind", "/etc/fonts", "/etc/fonts"]
    if source.exists():
        args.extend(["--ro-bind", str(source), "/in/" + source.name])
    for name in outputs:
        if name == "images":
            (dest / name).mkdir()
    args.extend(["--bind", str(dest), "/out", "--chdir", "/out", "--ro-bind", str(program),
                 "/converter", "/converter", "/in/" + source.name, *["/out/" + n for n in outputs]])
    p = subprocess.run(args, capture_output=True, stdin=subprocess.DEVNULL, timeout=20)
    # A crash, timeout or sandbox startup failure cannot satisfy an expected rejection.
    if p.returncode < 0 or p.returncode >= 126 or p.stderr.startswith(b"bwrap:"):
        raise AssertionError(f"unusable exit {p.returncode}: {p.stderr[:300]!r}")
    return p.returncode


def record(name, fn, *args):
    try:
        fn(*args)
        checks[name] = {"pass_": True}
    except (AssertionError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        checks[name] = {"pass_": False, "detail": str(error)[:400]}


def pixels(actual, expected, tolerance):
    assert actual.size == expected.size, f"wrong dimensions: {actual.size}, expected {expected.size}"
    diff = ImageChops.difference(actual.convert("RGB"), expected.convert("RGB"))
    error = max(ImageStat.Stat(diff).mean)
    assert error < tolerance, f"channel mean error {error:.2f}, limit {tolerance}"


def ppm(label):
    source = fixtures / (label + ".jpg")
    dest = work / label
    assert invoke(source, dest, "output.ppm") == 0, "conversion failed"
    path = dest / "output.ppm"
    assert path.read_bytes()[:2] == b"P6", "expected binary P6"
    with Image.open(source) as original, Image.open(path) as actual:
        expected = ImageOps.mirror(original) if case == "mirror" else original
        pixels(actual, expected, 4)


def textstats(source, expected):
    dest = work / source.stem
    assert invoke(source, dest, "output.txt", "counts.json") == 0, "conversion failed"
    assert (dest / "output.txt").read_text().split() == expected.split(), "wrong text or order"
    counts = json.loads((dest / "counts.json").read_text())
    assert isinstance(counts, dict) and all(type(v) is int for v in counts.values()), "invalid counts"
    assert counts == Counter(re.findall("[a-z]+", expected.lower())), f"wrong counts: {counts}"


def pages(source, refs):
    dest = work / source.stem
    assert invoke(source, dest, "images") == 0, "rendering failed"
    paths = sorted((dest / "images").iterdir())
    assert [p.name for p in paths] == [f"page-{i}.jpg" for i in range(1, len(refs)+1)], "wrong page names or count"
    for path, reference in zip(paths, refs):
        with Image.open(path) as actual, Image.open(reference) as expected:
            assert actual.format == "JPEG", "wrong encoding"
            pixels(actual, expected, 4)


def ledger(label, lines, expected):
    source = work / (label + ".txt")
    source.write_text(lines)
    dest = work / label
    rc = invoke(source, dest, "result.json")
    path = dest / "result.json"
    if expected is None:
        assert rc != 0 and not path.exists(), "invalid input accepted or output left behind"
    else:
        assert rc == 0, "valid input rejected"
        data = json.loads(path.read_text())
        assert isinstance(data, list) and all(type(v) is int for v in data) and data == expected, f"wrong totals: {data}"


def missing(outputs):
    assert invoke(work / "does not exist", work / "missing", *outputs) != 0, "missing input accepted"


if case in ("plain", "mirror"):
    outputs = ["output.ppm"]
    for label in ("wide source", "upright"):
        record(label, ppm, label)
elif case == "textstats":
    outputs = ["output.txt", "counts.json"]
    source = work / "repeated words.pdf"
    surface = cairo.PDFSurface(str(source), 400, 400)
    ctx = cairo.Context(surface)
    lines = ["One, ONE red-blue.", "Red! one 42 blue."]
    for line in lines:
        ctx.move_to(20, 75)
        ctx.show_text(line)
        ctx.show_page()
    surface.finish()
    for pdf, expected in [(source, " ".join(lines)), (fixtures/"image-only.pdf", ""),
                          (fixtures/"mixed pages.pdf", "ORCHARD FIRST RIVER SECOND")]:
        record(pdf.name, textstats, pdf, expected)
elif case == "pages":
    outputs = ["images"]
    for label, count in [("mixed pages", 2), ("text-only", 1), ("image-only", 1)]:
        source = fixtures / (label + ".pdf")
        prefix = work / ("reference-" + label)
        subprocess.run(["pdftoppm", "-r", "72", "-png", str(source), str(prefix)], check=True)
        refs = sorted(work.glob(prefix.name + "-*.png"))
        if len(refs) != count:
            raise RuntimeError("Invalid reference render")
        record(label, pages, source, refs)
elif case == "ledger":
    outputs = ["result.json"]
    cases = {"mixed": ("BEGIN\nADD +10\nADD -13\nEND\nBEGIN\nADD 9\nVOID\nBEGIN\nEND\nBEGIN\nADD 91", [-3, 0]),
             "empty": ("", []), "unterminated-line": ("BEGIN\nADD -4\nEND", [-4]),
             "orphan-add": ("ADD 7\n", None), "orphan-void": ("VOID\n", None),
             "orphan-end": ("END\n", None), "nested": ("BEGIN\nBEGIN\n", None),
             "late-error": ("BEGIN\nADD 7\nEND\nNOPE\n", None),
             "bad-integer": ("BEGIN\nADD 2x\nEND\n", None)}
    for label, (lines, expected) in cases.items():
        record(label, ledger, label, lines, expected)
elif case == "clarify":
    outputs = []
    checks["clarification before build"] = {"pass_": False, "detail": "construction attempted before user choice"}
else:
    raise ValueError(case)
if outputs:
    record("missing input", missing, outputs)
print(json.dumps({"checks": checks}, indent=2))
