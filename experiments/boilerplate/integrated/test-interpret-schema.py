"""Regression checks for the optional Ollama decoder. Uses the installed jsonschema validator."""
import json
import subprocess
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator


pilot = Path(__file__).resolve().parent
providers = pilot.parent / "component-trial" / "providers"
contracts = json.loads((providers / "contracts.json").read_text())
command = ["jq"]
for name, path in (
    ("providers", providers / "contracts.json"),
    ("adapters", pilot / "adapters.json"),
    ("runtime", pilot / "cli-guarantees.json"),
    ("library", providers / "library.json"),
):
    command.extend(["--slurpfile", name, str(path)])
command.extend(["-f", str(pilot / "interpret-schema.jq")])
vocabulary = {"profile": "sequence", "outcomes": contracts["outcomes"]}
schema = json.loads(subprocess.check_output(command, input=json.dumps(vocabulary), text=True))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)

requirement = {
    "operations": [{"outcome": "ordered_column", "parameters": {"column": 0}}],
    "runtime": ["arguments"], "uncovered": [],
}
role = {
    "id": "r1", "text": "write the selected column as JSON", "multiple": False,
    "content": "whole", "output_type": "application/json", "requirement": requirement,
}
responsibilities = [{"id": "a", "text": "select one column"}]
cli = {"input_type": "text/tab-separated-values", "roles": [role], "responsibilities": responsibilities}
header = {
    "artifact": {
        "kind": "header", "filename": "queue.hpp", "class_name": "Queue", "value_type": "string",
        "methods": {"add": "insert_unique"}, "uncovered": [],
    },
    "responsibilities": responsibilities,
}
threshold = {
    "key_column": 0, "value_column": 1, "trigger": "le", "threshold": 2,
    "reset": "ge", "reset_threshold": 5, "emit": "key",
}


def threshold_request(parameters):
    req = {"outcome": "threshold_entries", "parameters": parameters, "runtime": [], "uncovered": []}
    return {**cli, "roles": [{**role, "requirement": req}]}

for accepted in (
    cli, {**cli, "artifact": {"kind": "cli"}}, header, {"clarify": "Which output format?"},
    threshold_request(threshold),
    {**cli, "roles": [{**role, "output_type": "text/plain"}]},  # Legacy converter sink
    {**cli, "roles": [{**role, "output_type": "other", "requested_output_type": "text/csv"}]},
):
    validator.validate(accepted)

for rejected in (
    {**cli, "artifact": {"kind": "header"}},
    {**cli, "roles": [{**role, "output_type": "text/tab-separated-values"}]},  # Input-only port
    {**cli, "roles": [{**role, "output_type": "other"}]},
    {**cli, "roles": [{**role, "requirement": {**requirement, "operations": [
        {"outcome": "ordered_column", "parameters": {"column": "0 or 1"}},
    ]}}]},
    {**header, "artifact": {**header["artifact"], "methods": {"add": "sort_queue"}}},
    threshold_request({**threshold, "emit": "key or row_number"}),
):
    if validator.is_valid(rejected):
        raise AssertionError(f"Unexpectedly accepted {rejected}")

# In this base catalog original_bytes is exclusively a binary file writer.
# The same effect in an extended catalog may also have an intermediate value.
writer = {"outcome":"original_bytes", "parameters":{}}
def role_with_output(output, content):
    return {**cli, "roles": [{**role, "output_type": output, "requirement": content}]}


single_writer = {**writer, "runtime": [], "uncovered": []}
ordered_writer = {"operations": [writer], "runtime": [], "uncovered": []}
for content in (single_writer, ordered_writer):
    validator.validate(role_with_output("application/octet-stream", content))
    if validator.is_valid(role_with_output("application/json", content)):
        raise AssertionError("A binary-only file writer was admitted for a JSON output")

with tempfile.TemporaryDirectory() as tmp:
    extended = Path(tmp) / "providers.json"
    contracts["providers"].append({"effect": "original_bytes", "to": "decoded_bytes"})
    extended.write_text(json.dumps(contracts))
    with_intermediate = command.copy()
    with_intermediate[with_intermediate.index(str(providers / "contracts.json"))] = str(extended)
    schema = json.loads(subprocess.check_output(with_intermediate, input=json.dumps(vocabulary), text=True))
    Draft202012Validator(schema).validate(role_with_output("application/json", ordered_writer))
