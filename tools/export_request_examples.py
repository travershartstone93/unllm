#!/usr/bin/env python3
"""Export unchanged CLI requests only after independent interaction checks pass."""
from pathlib import Path
import hashlib
import json
import shutil


ROOT = Path(__file__).resolve().parents[1]
FOLDER = ROOT / "examples/upgrade/requests"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    requests_path = ROOT / "tests/upgrade_requests.json"
    requests = json.loads(requests_path.read_text())
    expected = {(row["id"], language): row["question"]
                for row in requests for language in ("python", "cpp")}
    generated = json.loads((ROOT / ".benchmarks/request_fix_trial.json").read_text())
    verified = json.loads((ROOT / ".benchmarks/request_fix_verified.json").read_text())
    for report in (generated, verified):
        if report["requests_sha256"] != digest(requests_path):
            raise ValueError("The original request fixture changed")
    if verified["workflows_sha256"] != digest(ROOT / "tests/upgrade_request_interactions.json"):
        raise ValueError("The independent interaction fixture changed")
    jobs = {(row["id"], row["language"]): row for row in generated["results"]}
    probes = {(row["task"], row["language"]): row for row in verified["results"]}
    if (set(jobs) != set(expected) or set(probes) != set(expected)
            or len(generated["results"]) != len(expected)
            or len(verified["results"]) != len(expected)):
        raise ValueError("Every original request needs both language outputs")
    # Check the whole batch before copying any output.
    for key, question in expected.items():
        row, probe = jobs[key], probes[key]
        result = row["response"]
        source = Path(result["source"])
        if (row["question"] != question or probe["question"] != question
                or result["status"] != "validated" or not probe.get("passed")
                or not all(check["passed"] for check in probe["checks"])
                or probe["job"] != result["job"]
                or digest(source) != result["source_sha256"][source.name]
                or digest(source) != probe["source_sha256"]):
            raise ValueError("Source or validation mismatch: " + str(key))

    FOLDER.mkdir(parents=True, exist_ok=True)
    outputs = []
    for (task, language), question in expected.items():
        result, probe = jobs[task, language]["response"], probes[task, language]
        source, job = Path(result["source"]), Path(result["job"])
        target = FOLDER / task
        target.mkdir(exist_ok=True)
        shutil.copyfile(source, target / source.name)
        if language == "python":
            (target / "specification.json").write_text(
                json.dumps(result["specification"], ensure_ascii=False, indent=2) + "\n")
            shutil.copyfile(job / "requirements.txt", target / "requirements.txt")
            shutil.copyfile(probe["preview"], target / "preview.png")
        else:
            cache = ROOT / ".state/request-examples" / task
            cache.mkdir(parents=True, exist_ok=True)
            shutil.copy2(job / "program", cache / "program")
            (cache / "program").touch()
        attempt = result["attempts"][0]
        outputs.append({
            "task": task, "language": language, "question": question,
            "file": str((target / source.name).relative_to(FOLDER)),
            "source_sha256": digest(source), "validated_job": str(job),
            "generation_seconds": jobs[task, language]["seconds"],
            "independent_checks": len(probe["checks"]), "independent_passed": True,
            "executable_checks": attempt["cases"], "sanitizers": attempt["sanitizers"],
            "solver": result["solver"], "structure": result["structure"],
        })
    manifest = {
        "requests_sha256": verified["requests_sha256"],
        "workflows_sha256": verified["workflows_sha256"],
        "blinded": False, "method": verified["method"], "outputs": outputs,
    }
    (FOLDER / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    (FOLDER / "README.md").write_text("""# Programs generated from the six original requests

All six English requests from the earlier manual trial now generate working
outputs in both Python/PySide6 and C++/Qt6. The request text is unchanged.
Each output passed ordinary validation and a separate Qt keyboard/mouse/file
workflow. The examples are exported outputs; the generator never reads them.
This is development evaluation within the documented vocabulary and primitives.

Run from the upgrade checkout with a desktop display:

    ./examples/upgrade/requests/run.sh todo python
    ./examples/upgrade/requests/run.sh stopwatch cpp
    ./examples/upgrade/requests/run.sh tip python

Other example names are text_counter, decrement and temperature. Each folder
contains both languages, its typed specification, Python requirements and a
screenshot. The manifest records the original request, source hashes and checks.
The launcher uses the checkout's pinned Python environment, or a local C++ cache.
It rebuilds missing/outdated C++ executables using Clang and Qt6 Widgets.

To regenerate a request and run the independent workflows:

    .venv/bin/python tools/verify_requests.py
    .venv/bin/python tools/export_request_examples.py

Generated programs run without unllm, Prolog, cvc5 or a model. Python needs
its listed packages; C++ needs Qt6 runtime libraries. See the
[repair report](../../../docs/REQUEST_REPAIR.md) for exact outcomes and limits.
""")
    print(f"Exported {len(outputs)} independently checked outputs to {FOLDER}")


if __name__ == "__main__":
    main()
