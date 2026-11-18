#!/usr/bin/env python3
"""Export validated calculator outputs, without making them generator inputs."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "examples/upgrade/calculator"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    benchmark = json.loads((ROOT / ".benchmarks/applications.json").read_text())
    tasks = {t["id"]: t for t in json.loads((ROOT / "tests/upgrade_apps.json").read_text())["tasks"]}
    FOLDER.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name in ("standard", "memory"):
        (FOLDER / (name + ".spec.json")).write_text(json.dumps(tasks[name]["spec"], indent=2) + "\n")
        for language in ("python", "cpp"):
            row = next(r for r in benchmark["results"] if r["task"] == name and r["language"] == language)
            job = Path(row["job"])
            report = json.loads((job / "report.json").read_text())
            if row["status"] != "validated" or report["status"] != "validated" or not row["fresh_gui"]["passed"]:
                raise ValueError("Only validated outputs with a passing fresh GUI check may be exported")
            target = FOLDER / name / language
            target.mkdir(parents=True, exist_ok=True)
            names = ["program.py"] if language == "python" else ["program.cpp"]
            names += sorted(p.name for p in job.glob("Arithmetic*") if p.is_file())
            names += ["ANTLR-LICENSE.txt", "application.ir.json", "specification.json"]
            if language == "python":
                names.append("requirements.txt")
            hashes = {}
            for filename in names:
                source = job / filename
                expected = report["source_sha256"].get(filename)
                if expected is not None and expected != digest(source):
                    raise ValueError("Validated source changed: " + str(source))
                shutil.copyfile(source, target / filename)
                hashes[filename] = digest(target / filename)
            shutil.copyfile(report["preview"], target / "preview.png")
            hashes["preview.png"] = digest(target / "preview.png")
            if language == "cpp":
                # Keep executables out of the exported source tree. The launcher
                # can also rebuild this local cache from the exported sources.
                cache = ROOT / ".state/calculator-examples" / name
                cache.mkdir(parents=True, exist_ok=True)
                shutil.copy2(job / "program", cache / "program")
                (cache / "program").touch()
            checks = report["attempts"][0]
            manifest.append({
                "variant": name, "language": language, "directory": str(target.relative_to(FOLDER)),
                "validated_job": str(job), "source_sha256": hashes,
                "application_lines": report["meaningful_lines"],
                "arithmetic_cases": checks["arithmetic_cases"], "gui_scenarios": checks["gui_scenarios"],
                "checks": checks["cases"], "sanitizers": checks["sanitizers"],
                "fresh_gui": row["fresh_gui"], "dependencies": report["dependencies"],
                "grammar_sha256": report["grammar"], "structure": report["structure"],
            })
    (FOLDER / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (FOLDER / "README.md").write_text("""# Generated calculator examples

These are exported Python/PySide6 and C++/Qt6 calculator applications. Both variants
support algebraic precedence, decimals, unary signs, parentheses, clear, backspace
and keyboard input. The memory variant adds M+, M-, MR and MC.
The generator never reads this directory as an answer catalog.

From the upgrade checkout:

    ./examples/upgrade/calculator/run-python.sh
    ./examples/upgrade/calculator/run-python.sh memory
    ./examples/upgrade/calculator/run-cpp.sh
    ./examples/upgrade/calculator/run-cpp.sh memory

A desktop display is required. The Python launcher uses the checkout's pinned
virtual environment. The C++ launcher uses an ignored local executable cache and
rebuilds it from these sources when needed, using Qt6 Widgets development files,
Clang and the locally installed ANTLR 4.13.2 static runtime. No unllm worker,
solver or model runs inside either generated application.

To generate another variant:

    ./build/unllm --code python --spec examples/upgrade/calculator/memory.spec.json
    ./build/unllm --code cpp 'Make a standard calculator app.'

Python can also run outside this checkout after installing its exported
requirements.txt. For a separate C++ build, compile program.cpp and Arithmetic*.cpp
with C++20, -DANTLR4CPP_STATIC, Qt6 Widgets and the ANTLR 4.13.2 runtime.
The bundled grammar and ANTLR license accompany each parser.

The manifest records source hashes, specifications, validation jobs and checks.
Each output passed an independent arithmetic oracle and actual Qt mouse/keyboard
interactions; C++ passed normal and address/undefined-behavior sanitizer builds.
Fresh GUI actions were also checked after construction. These are finite
development checks, not a general correctness proof or blind app-generation score.

See [the implementation report](../../../docs/CALCULATOR_UPGRADE.md) for bounds.
Regenerate these exports with .venv/bin/python tools/benchmark_apps.py followed by
.venv/bin/python tools/export_app_examples.py.
""")
    print(json.dumps([{k: row[k] for k in ("variant", "language", "checks")} for row in manifest], indent=2))


if __name__ == "__main__":
    main()
