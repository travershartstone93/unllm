#!/usr/bin/env python3
"""Explicit opt-in comparison; no changes to the unllm CLI/default routing."""
from pathlib import Path
import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from archive.local_interpreter.interpreter import ModelConfig, extract, request_json, prompt_hash
from archive.local_interpreter.oracle import score
from upgrade.engine import Engine
from upgrade.model import Unsupported, Ambiguous
from synthesis.language import interpret
from synthesis.apps.validate import sandbox


def grammar(question, engine):
    started = time.monotonic()
    try:
        meaning = engine.interpret(question, engine.context("experiment"), "experiment", programming=True)
        result = {"status": "accepted", "specification": interpret(meaning).data(), "diagnostics": meaning.diagnostics}
    except Ambiguous as error:
        result = {"status": "ambiguous", "error": str(error)}
    except Unsupported as error:
        result = {"status": "unsupported", "error": str(error)}
    except Exception as error:
        result = {"status": "error", "error": f"{type(error).__name__}: {error}"}
    return {**result, "seconds": round(time.monotonic() - started, 6)}


def external_workflow(response, workflow, language):
    job, source = Path(response["job"]), Path(response["source"])
    if hashlib.sha256(source.read_bytes()).hexdigest() != response["source_sha256"][source.name]:
        raise RuntimeError("Candidate source changed after validation.")
    if language == "python":
        shutil.copyfile(ROOT / "tools/probes/request_python.py", job / "independent_probe.py")
        command = ["/runtime/python/bin/python3.12", "/job/independent_probe.py"]
    else:
        shutil.copyfile(ROOT / "tools/probes/request_cpp.cpp", job / "independent_probe.cpp")
        flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Widgets", "Qt6Test"], text=True).split()
        compilation = subprocess.run(["clang++", "-std=c++20", "-O0", str(job / "independent_probe.cpp"),
                                      *flags, "-o", str(job / "independent_probe")], capture_output=True, text=True, timeout=60)
        if compilation.returncode:
            return {"passed": False, "stage": "probe_compile", "stderr": compilation.stderr[-4000:]}
        command = ["/job/independent_probe"]
    folder = job / "experiment-output"
    folder.mkdir(exist_ok=True)
    execution = sandbox(job, command, workflow, folder, python=language == "python", timeout=30)
    if execution.returncode:
        return {"passed": False, "stage": "probe_execute", "stderr": execution.stderr[-4000:], "stdout": execution.stdout[-1000:]}
    return json.loads(execution.stdout)


def timing(values):
    if not values:
        return None
    return {"n": len(values), "p50_seconds": round(statistics.median(values), 6),
            "p95_seconds": round(sorted(values)[math.ceil(.95 * len(values)) - 1], 6),
            "max_seconds": round(max(values), 6)}


def totals(rows, modes):
    result = {}
    for mode in modes:
        cases = [row[mode] for row in rows]
        supported = [row[mode] for row in rows if row["expected_status"] == "accepted"]
        refused = [row[mode] for row in rows if row["expected_status"] != "accepted"]
        accepted = [r for r in cases if r["status"] == "accepted"]
        result[mode] = {"attempted": len(cases), "correct": sum(r["score"]["correct"] for r in cases),
            "supported_correct": sum(r["score"]["correct"] for r in supported), "supported_attempted": len(supported),
            "safe_refusals": sum(r["score"]["correct"] for r in refused), "refusal_attempted": len(refused),
            "accepted": len(accepted), "wrong_acceptances": sum(r["score"]["false_accept"] for r in cases),
            "errors": sum(r["status"] == "error" for r in cases), "interpretation_latency": timing([r["seconds"] for r in cases]),
            "by_group": {group: {"n": len(selected), "correct": sum(r[mode]["score"]["correct"] for r in selected)}
                         for group in sorted({r["group"] for r in rows}) if (selected := [r for r in rows if r["group"] == group])}}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", default=os.environ.get("UNLLM_INTERPRETER_MODEL"), help="Required for model mode; no built-in model name")
    parser.add_argument("--base-url", default=os.environ.get("UNLLM_INTERPRETER_URL", "http://127.0.0.1:11434"))
    parser.add_argument("--mode", choices=("grammar", "model", "both"), default="both")
    parser.add_argument("--question", help="Interpret one request instead of running the comparison fixture")
    parser.add_argument("--suite", type=Path, default=Path(__file__).with_name("cases.json"))
    parser.add_argument("--output", type=Path, help="Defaults to a new dated .benchmarks report")
    parser.add_argument("--execute", action="store_true", help="Build the selected correctly interpreted requests and drive independent Qt workflows")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--context", type=int, default=8192)
    parser.add_argument("--max-tokens", type=int, default=1400)
    parser.add_argument("--thinking", choices=("off", "on", "default"), default="off")
    parser.add_argument("--language", choices=("python", "cpp"), help="Build a single --question after interpretation")
    args = parser.parse_args()
    modes = ("grammar", "model") if args.mode == "both" else (args.mode,)
    if "model" in modes and not args.model:
        parser.error("Choose --model or UNLLM_INTERPRETER_MODEL. This experiment has no default model.")
    config = ModelConfig(args.model, args.base_url, args.timeout, args.context, args.max_tokens,
                         think={"off": False, "on": True, "default": None}[args.thinking]) if args.model else None
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    folder = ROOT / ".state/local-interpreter" / stamp
    folder.mkdir(parents=True)
    output = args.output or ROOT / ".benchmarks" / ("local_interpreter_" + stamp + ".json")
    output.parent.mkdir(parents=True, exist_ok=True)
    engine = Engine(folder / "facts.sqlite")
    try:
        if args.question:
            results = {mode: grammar(args.question, engine) if mode == "grammar" else extract(args.question, config) for mode in modes}
            if args.language:
                for mode, result in results.items():
                    if result["status"] == "accepted":
                        result["program"] = engine.ask({"spec": result["specification"], "language": args.language, "context": "single-" + mode})
            output.write_text(json.dumps(results, indent=2, ensure_ascii=False) + "\n")
            print(json.dumps(results, indent=2, ensure_ascii=False))
            return
        fixture_bytes = args.suite.read_bytes()
        fixture = json.loads(fixture_bytes)
        fixture_hash = hashlib.sha256(fixture_bytes).hexdigest()
        implementation = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.glob("*.py")}
        model_meta = None
        if "model" in modes:
            tags = request_json(config, "/api/tags")["models"]
            entry = next((row for row in tags if row["name"] == config.model or row["model"] == config.model), None)
            if entry is None:
                raise RuntimeError("Select an already installed local model; the experiment does not download one.")
            model_meta = {"configuration": asdict(config), "installed": entry,
                          "server": request_json(config, "/api/version"), "initially_loaded": request_json(config, "/api/ps")}
        report = {"started_utc": stamp, "blinded": False, "scope": fixture["purpose"],
                  "model": model_meta, "prompt_sha256": prompt_hash(), "suite_sha256": fixture_hash,
                  "implementation_sha256": implementation, "results": [], "execution": [], "artifacts": str(folder)}
        def save():
            report["totals"] = totals(report["results"], modes)
            output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
        for case in fixture["cases"]:
            row = {"id": case["id"], "group": case["group"], "question": case["question"], "expected_status": case["expected"]["status"]}
            for mode in modes:
                try:
                    result = grammar(case["question"], engine) if mode == "grammar" else extract(case["question"], config)
                except Exception as error:
                    result = {"status": "error", "seconds": config.timeout if config else 0, "error": f"{type(error).__name__}: {error}"}
                result["score"] = score(result, case["expected"])
                row[mode] = result
                print(case["id"], mode, result["status"], "correct=" + str(result["score"]["correct"]), result["seconds"], flush=True)
                if not result["score"]["correct"]:
                    print("  ", result.get("error") or result["score"].get("errors", [])[:3], flush=True)
            report["results"].append(row)
            save()
        if args.execute:
            lookup = {row["id"]: row for row in report["results"]}
            for case in fixture["cases"]:
                for language in case.get("execute", []):
                    for mode in modes:
                        result = lookup[case["id"]][mode]
                        row = {"case": case["id"], "mode": mode, "language": language, "semantic_correct": result["score"]["correct"]}
                        if not row["semantic_correct"]:
                            row.update(passed=False, status="skipped_incorrect_interpretation")
                        else:
                            started = time.monotonic()
                            try:
                                generated = engine.ask({"spec": result["specification"], "language": language, "context": "build-" + mode})
                                row.update(status=generated["status"], job=generated.get("job"), source=generated.get("source"),
                                           generation_seconds=round(time.monotonic() - started, 3))
                                row["validation"] = generated.get("attempts")
                                if generated["status"] == "validated":
                                    row["workflow"] = external_workflow(generated, case["workflow"], language)
                                    row["passed"] = row["workflow"]["passed"]
                                else:
                                    row["passed"] = False
                            except Exception as error:
                                row.update(passed=False, status="error", error=str(error))
                        report["execution"].append(row)
                        print("execute", case["id"], mode, language, row["status"], row["passed"], flush=True)
                        save()
        report["suite_unchanged"] = hashlib.sha256(args.suite.read_bytes()).hexdigest() == fixture_hash
        report["implementation_unchanged"] = implementation == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in Path(__file__).parent.glob("*.py")}
        report["completed_utc"] = datetime.now(timezone.utc).isoformat()
        save()
        print(json.dumps(report["totals"], indent=2))
        print("Report:", output)
        if not report["suite_unchanged"] or not report["implementation_unchanged"]:
            raise RuntimeError("Evaluation inputs or implementation changed during the run.")
    finally:
        engine.parser.close()
        engine.store.db.close()


if __name__ == "__main__":
    main()
