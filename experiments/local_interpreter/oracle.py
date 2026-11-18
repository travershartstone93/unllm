"""Compare interpreted contracts with independently annotated request outcomes.

Expected requirements are never provided to either interpreter. This observer
samples scalar solutions before compilation; selected apps additionally run an
external Qt workflow. It does not score a candidate against its own description.
"""
import ast
import json
import math
from synthesis.behaviour.spec import BehaviourSpec
from synthesis.behaviour.solver import synthesize
from synthesis.behaviour.expressions import evaluate

NUMBER_INPUTS = (-7.25, 0, 0.125, 9)
RATE_INPUTS = ((123.45, 12.5), (80, 0), (0.01, 15))
UNIT_INPUTS = (0, 17.5, 100)


def observe(raw):
    spec = BehaviourSpec.from_dict(raw)
    views = {v["id"]: v for v in spec.views}
    def kind(name):
        return views.get(name, {}).get("kind")
    record_fields = spec.state.get("records", {}).get("fields")
    result = {
        "state_types": {name: value["type"] for name, value in spec.state.items()},
        "text": "multiline" if kind("body_view") == "text_edit" else "line" if kind("body_view") == "line_edit" else "none",
        "metrics": sorted(v["value"].split("(")[0] for v in spec.views if v["kind"] == "label" and v.get("value") in ("chars(body)", "words(body)", "lines(body)")),
        "counter_initial": spec.state.get("value", {}).get("initial"),
        "record_fields": record_fields,
        "periodic_clock": bool(spec.value.get("events")),
        "percentage_label": views.get("part_output", {}).get("label"),
        "source_label": views.get("source_input", {}).get("label"),
        "target_label": views.get("converted_output", {}).get("label"),
        "actions": [],
    }
    # Observe every visible action so unrequested additions cannot disappear.
    for action in spec.actions:
        if not action.get("visible", True):
            continue
        if "effect" in action:
            result["actions"].append({"effect": action["effect"], "target": action["state"]})
        elif "assign" in action:
            assignments = {}
            for target, expression in action["assign"].items():
                assignments[target] = ast.dump(ast.parse(expression, mode="eval").body, include_attributes=False)
            result["actions"].append({"assign": assignments})
        else:
            updates, _ = synthesize(action, spec.state)
            state = {name: value["initial"] for name, value in spec.state.items()}
            outputs = set(updates)
            samples = []
            if outputs == {"value"}:
                for value in NUMBER_INPUTS:
                    samples.append(evaluate(updates["value"], {**state, "value": value}))
            elif outputs == {"part", "total"}:
                for amount, rate in RATE_INPUTS:
                    row = {**state, "amount": amount, "rate": rate}
                    samples.append([evaluate(updates[n], row) for n in ("part", "total")])
            elif outputs == {"output_value"}:
                for value in UNIT_INPUTS:
                    samples.append(evaluate(updates["output_value"], {**state, "input_value": value}))
            else:
                raise ValueError("Unannotated scalar output set: " + str(outputs))
            result["actions"].append({"outputs": sorted(outputs), "samples": samples})
    result["actions"].sort(key=lambda row: json.dumps(row, sort_keys=True))
    return result


def differences(actual, expected, path=""):
    if type(expected) in (float, int) and type(actual) in (float, int):
        return [] if math.isclose(actual, expected, rel_tol=1e-10, abs_tol=1e-9) else [f"{path}: expected {expected!r}, got {actual!r}"]
    if isinstance(expected, dict) and isinstance(actual, dict):
        if set(expected) != set(actual):
            return [f"{path}: expected keys {sorted(expected)}, got {sorted(actual)}"]
        return [error for key in expected for error in differences(actual[key], expected[key], path + "." + key)]
    if isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            return [f"{path}: expected {len(expected)} items, got {len(actual)}"]
        return [error for i, (a, e) in enumerate(zip(actual, expected)) for error in differences(a, e, path + f"[{i}]")]
    return [] if actual == expected else [f"{path}: expected {expected!r}, got {actual!r}"]


def score(result, expected):
    accepted = result.get("status") == "accepted"
    if expected["status"] != "accepted":
        return {"correct": not accepted and result.get("status") in ("unsupported", "ambiguous"),
                "false_accept": accepted, "expected_status": expected["status"]}
    if not accepted:
        return {"correct": False, "false_accept": False, "errors": ["Expected a supported interpretation."]}
    try:
        observed = observe(result["specification"])
        errors = differences(observed, expected["contract"])
        return {"correct": not errors, "false_accept": bool(errors), "observed": observed, "errors": errors}
    except Exception as error:
        return {"correct": False, "false_accept": True, "errors": [f"{type(error).__name__}: {error}"]}
