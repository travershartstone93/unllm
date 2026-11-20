"""Build a small test fixture from the repository sources. Never reads the production corpus."""
import json
import shutil
import sys
from pathlib import Path

trial = Path(__file__).resolve().parents[2] / "component-trial"
bundle = Path(sys.argv[1])
bundle.mkdir()
(bundle / "headers").mkdir()
shutil.copytree(trial / "providers", bundle / "providers")
cards = json.loads((trial / "components.json").read_text())
for card in cards:
    shutil.copyfile(trial / card["source"], bundle / "headers" / card["header"])
schema_path = bundle / "providers/contracts.json"
schema = json.loads(schema_path.read_text())
# Use the same operation records and C++ as the shipped expansion.
for index, row in enumerate((trial / "expanded/operations.tsv").read_text().splitlines()[1:], 1):
    effect, input_type, output_type, function, params, description = row.split("|")
    if effect not in {"sort_lines", "reverse_order", "byte_lengths"}:
        continue
    assert not params
    card = dict(id=f"x{index:02}", header="text_ops.hpp", source="expanded/text_ops.hpp", packages=["Qt6Core"],
                operation=effect, description=description, outputs=[description])
    cards.append(card)
    schema["outcomes"][effect] = dict(meaning=description, parameters={})
    schema["providers"].append({"id":effect, "brick":card["id"], "effect":effect, "from":input_type,
                                "to":output_type, "header":card["header"], "call":function})
# The shipped expansion registers these representation adapters with its first text card.
schema["providers"].extend([
    {"id":"read_lines","brick":"x09","from":"in:text/plain","to":"strings","header":"text_ops.hpp","call":"bricks::lines"},
    {"id":"write_lines","brick":"x09","from":"strings","to":"out:text/plain","header":"text_ops.hpp","call":"bricks::save_lines"},
])
shutil.copyfile(trial / "expanded/text_ops.hpp", bundle / "headers/text_ops.hpp")
schema_path.write_text(json.dumps(schema))
catalog = [{k: c[k] for k in ("id","description","outputs")} for c in cards]
for name, obj in [("components", cards), ("catalog", catalog), ("reviews", {"catalog":catalog})]:
    (bundle / (name + ".json")).write_text(json.dumps(obj))
