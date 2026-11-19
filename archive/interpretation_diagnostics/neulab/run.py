"""Run in the upstream checkout with its Python 3.7 environment and PYTHONPATH=."""
import contextlib
import csv
import json
import os
import sys
from pathlib import Path

import torch
from components.standalone_parser import StandaloneParser

root = Path(__file__).resolve().parents[3]
area = Path(__file__).resolve().parent
config = json.loads(Path("config/config_conala.json").read_text())["conala"]
torch.set_num_threads(1)
torch.manual_seed(0)
parser = StandaloneParser(config["parser"], config["model_path"], config["example_processor"],
                          beam_size=config["beam_size"], reranker_path=config["reranker_path"], cuda=False)
requests = [("upstream", "reverse a list")]
with (area.parent / "n16/evaluation.tsv").open() as f:
    requests.extend((r["id"], r["prompt"]) for r in csv.DictReader(f, delimiter="\t"))
phrasing = root / "experiments/algorithm_ir/contract_trial/phrasing.json"
requests.extend(("phrase%d" % i, row["request"]) for i, row in enumerate(json.loads(phrasing.read_text()), 1))
requests.append(("named", "Count the values in list `values` greater than `cutoff`."))
with (area / "predictions.jsonl").open("w") as out, open(os.devnull, "w") as sink, torch.no_grad():
    for name, prompt in requests:
        with contextlib.redirect_stdout(sink):
            hypotheses = parser.parse(prompt)
        row = dict(id=name, prompt=prompt, top=hypotheses[0].code if hypotheses else None, returned=len(hypotheses))
        print(json.dumps(row), file=out, flush=True)
