#!/usr/bin/env python3
"""Score B27's four recorded runs against the same external gold and fixtures."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("experiment", type=Path)
base = parser.parse_args().experiment

def read(path):
    return json.loads(path.read_text())

gold = read(base / "gold.json")

def score(selection, expected):
    bindings = selection["bindings"]
    assert set(bindings) == {f"s{i}" for i in range(1, len(expected) + 1)}
    rows = []
    for i, accepted in enumerate(expected, start=1):
        pick = bindings[f"s{i}"]
        rows.append({"pick": pick, "accepted": accepted, "correct": pick in (accepted or [None])})
    return rows

def edit_usage(run):
    usage = {"input": 0, "output": 0, "reasoning": 0, "cache_read": 0, "cache_write": 0}
    events = [json.loads(line) for line in (run/"calls/edit/events.jsonl").read_text().splitlines()]
    steps = [event["part"]["tokens"] for event in events if event["type"] == "step_finish"]
    if not steps:
        steps = read(run/"calls/edit/usage.json")["steps"]
    assert steps
    for step in steps:
        for key in ("input", "output", "reasoning"):
            usage[key] += step[key]
        for key in ("read", "write"):
            usage["cache_" + key] += step["cache"][key]
    return usage

report = {"arms": {}, "data_matched_current": {}}
for catalog in ("old", "expanded"):
    for mode in ("current", "trained"):
        arm = catalog + "-" + mode
        cases = {}
        all_roles = []
        tokens = {}
        for name, labels in gold.items():
            run = base/arm/name
            assert (run/"request.txt").read_bytes() == (base.parent/"boilerplate-B26"/name/"request.txt").read_bytes()
            picked = read(run/"components/selection.json")
            scored = score(picked, labels)
            all_roles.extend(scored)
            final = read(run/"result.json")
            checks = run/"edit-checks.json"
            external = read(checks)["checks"] if checks.exists() else {}
            mutation = base/"mutation"/arm/name/"use.json"
            used = [k for k,v in read(mutation).items() if v] if mutation.exists() else []
            helpers = read(run/"components/helpers.json")
            if final["passed"] and helpers:
                assert mutation.exists()
            qwen = run/"components/qwen.json"
            qdata = read(qwen) if qwen.exists() else {}
            usage = edit_usage(run)
            for k,v in usage.items():
                tokens[k] = tokens.get(k, 0) + v
            cases[name] = {
                "roles": scored, "passed": final["passed"], "completed_at": final["completed_at"],
                "passed_checks": sum(v["pass_"] for v in external.values()), "executed_checks": len(external),
                "stage5_used": final["stage5_used"], "staged": [h["id"] for h in helpers], "verified_used": used,
                "component_qwen_tokens": qdata.get("input_tokens", 0) + qdata.get("output_tokens", 0),
                "edit_origin": (run/"calls/edit/origin").read_text().strip(), "edit_usage": usage,
                "edit_prompt_sha256": hashlib.sha256((run/"calls/edit/prompt.txt").read_bytes()).hexdigest(),
            }
        queue = base/arm/"queue"
        queue_steps = [json.loads(line) for line in (queue/"steps.jsonl").read_text().splitlines()]
        assert queue_steps[-1]["stage"] == "clarify"
        assert (queue/"request.txt").read_bytes() == (base.parent/"boilerplate-B26/queue/request.txt").read_bytes()
        report["arms"][arm] = {
            "role_correct": sum(r["correct"] for r in all_roles), "roles": len(all_roles),
            "positive_correct": sum(r["correct"] and bool(r["accepted"]) for r in all_roles),
            "correct_abstentions": sum(r["correct"] and not r["accepted"] for r in all_roles),
            "programs_passed": sum(c["passed"] for c in cases.values()), "requests": len(cases)+1,
            "passed_checks": sum(c["passed_checks"] for c in cases.values()),
            "executed_checks": sum(c["executed_checks"] for c in cases.values()),
            "verified_task_brick_pairs": sum(len(c["verified_used"]) for c in cases.values()),
            "component_qwen_tokens": sum(c["component_qwen_tokens"] for c in cases.values()),
            "edit_tokens": dict(tokens, total=sum(tokens.values())), "cases": cases, "queue": queue_steps[-1],
        }
for name, labels in gold.items():
    chosen = read(base/"matched-current"/name/"selection.json")
    report["data_matched_current"][name] = score(chosen, labels)
controls = read(base/"controls.json")
report["controls"] = {
    size: {subset: {method: sum(r[method] in r["gold"] for r in stats[subset]) for method in ("nearest","trained")}
           for subset in ("b26","legacy")} for size, stats in controls.items()
}
report["controls_note"] = "B26 control scores condition on availability in each catalog. The primary arm scores use the fixed expanded-catalog gold across all arms."
report["diagnostics"] = {
    "fresh_inventory": {
        "selection": read(base/"live-inventory/components/selection.json"),
        "result": read(base/"live-inventory/result.json"),
        "checks": read(base/"live-inventory/edit-checks.json"),
        "disabled_helpers": read(base/"mutation/live-inventory/use.json"),
    },
    "scheduler_one_call_correction": read(base/"build-diagnostic/report.json"),
    "waiting_list": {
        "whole_request_selection": read(base/"queue-diagnostic/selection.json"),
        "decomposed_selection": read(base/"queue-diagnostic/components/selection.json"),
        "generated_header": read(base/"queue-diagnostic/report.json"),
        "disabled_pop": read(base/"queue-diagnostic/mutation/report.json"),
    },
}
print(json.dumps(report, indent=2))
