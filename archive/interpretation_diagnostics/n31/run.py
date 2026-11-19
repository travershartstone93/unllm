"""Paired 0/30/60 experiment on the frozen authored contrasts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import torch
from gliner2 import GLiNER2
from gliner2.training.data import Classification, InputExample
from gliner2.training.trainer import GLiNER2Trainer, TrainingConfig
from safetensors import safe_open
from safetensors.torch import load_file

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / "n25"))
import evaluate as adapter

MISSING = "missing information if clarification is needed"
adapter.LABELS[MISSING]["whether equality is included"] = "Should values equal to the boundary qualify?"
TRAIN_QUESTIONS = {
    "t11a":"whether equality is included", "t18a":"whether equality is included", "t23a":"whether equality is included",
    "t14b":"threshold value", "t19a":"threshold value", "t26a":"threshold value", "t30b":"threshold value",
    "t15b":"comparison direction", "t22b":"aggregation", "t27b":"aggregation",
}
TEST_QUESTIONS = {"d21":"whether equality is included","d22":"aggregation","h13":"comparison direction","h14":"aggregation"}
FIELD_TASKS = {"status":"request outcome","aggregate":"aggregation","collection":"collection parameter",
               "comparison":"each element compared to threshold","required_structure":"explicitly required implementation"}
# Gold span annotations for the written-out integers in the training split only.
SURFACES = {-3:"minus three",3:"three",6:"six",7:"seven",8:"Eight",9:"nine",13:"thirteen",14:"fourteen",18:"eighteen"}
BASE = adapter.ASSETS / "gli"

def folder(n):
    return Path((ROOT/f".state/n{n}.path").read_text().strip())

def records(path):
    return [json.loads(s) for s in path.read_text().splitlines()]

def training_example(row):
    gold, text = row["expected"], row["request"]
    classes = []
    def add(task, label):
        classes.append(Classification(task, list(adapter.LABELS[task]), label))
    for field, task in FIELD_TASKS.items():
        if field in gold:
            add(task, next(k for k,v in adapter.LABELS[task].items() if v == gold[field]))
    entities = {}
    if gold["status"] == "clarify":
        add(MISSING, TRAIN_QUESTIONS[row["id"]])
    else:
        add(MISSING, "none")
    if gold["status"] == "ok":
        threshold = gold["compare_to"]
        source = next(iter(threshold))
        add("threshold source", next(k for k,v in adapter.LABELS["threshold source"].items() if v==source))
        mentions = []
        if source == "literal":
            k = threshold["literal"]
            surface = SURFACES.get(k,str(k))
            assert re.search(r"(?<!\w)"+re.escape(surface)+r"(?!\w)",text), row
            mentions.append(surface)
        entities[adapter.LITERAL] = mentions
    return InputExample(text=text, classifications=classes, entities=entities)

def train(size, dest):
    rows=records(folder(30)/"train.jsonl")[:size]
    assert len(rows)==size and len({r["contrast_id"] for r in rows})==size//2
    examples=[training_example(r) for r in rows]
    model=GLiNER2.from_pretrained(str(BASE))
    config=TrainingConfig(output_dir=str(dest),num_epochs=2,batch_size=2,encoder_lr=1e-5,task_lr=5e-5,
                          fp16=False,bf16=False,num_workers=0,save_best=False,logging_steps=5,seed=42)
    metrics=GLiNER2Trainer(model,config).train(train_data=examples)
    assert metrics["total_steps"]==size
    (dest/"training.json").write_text(json.dumps(metrics,default=str))

def evaluate(size, dest):
    model=GLiNER2.from_pretrained(str(BASE)).cpu().eval()
    changed = 0
    if size:
        params=load_file(str(dest/"final/model.safetensors"))
        with safe_open(str(BASE/"model.safetensors"),framework="pt") as old:
            assert set(old.keys())==set(params)
            changed=sum(not torch.equal(old.get_tensor(k),v) for k,v in params.items())
            assert changed > 0
        model.load_state_dict(params,strict=True)
        del params
    schema=model.create_schema().entities({adapter.LITERAL:"The fixed integer used as the actual comparison threshold"})
    for key,labels in adapter.LABELS.items():
        schema.classification(key,list(labels))
    summary={"training_rows":size,"changed_tensors":changed}
    for split,path in [("dev",folder(30)/"dev.jsonl"),("regression",folder(26)/"holdout.jsonl")]:
        results=[]
        with torch.inference_mode():
            for row in records(path):
                raw=model.extract(row["request"],schema)
                spans=raw.get("entities",{}).get(adapter.LITERAL,[])
                answer=None
                error=None
                try:
                    answer=adapter.normalized({**raw,adapter.LITERAL:spans[0] if len(spans)==1 else None})
                    adapter.jsonschema.validate(answer,adapter.SCHEMA)
                except (KeyError,TypeError,ValueError,adapter.jsonschema.ValidationError) as exc:
                    error=str(exc).splitlines()[0]
                expected=row["expected"]
                if expected["status"]=="clarify":
                    expected={"status":"clarify","question":adapter.LABELS[MISSING][TEST_QUESTIONS[row["id"]]]}
                results.append(dict(**row,raw=raw,answer=answer,error=error,correct=error is None and answer==expected))
        (dest/f"{split}.jsonl").write_text("".join(json.dumps(r)+"\n" for r in results))
        summary[split]={"total":len(results),"exact":sum(r["correct"] for r in results),
            "valid":sum(r["error"] is None for r in results),
            "correct_by_status":{s:sum(r["correct"] for r in results if r["expected"]["status"]==s) for s in ("ok","clarify","unsupported")},
            "correct_ids":[r["id"] for r in results if r["correct"]]}
    (dest/"scores.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary))

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("action",choices=["train","eval"])
    parser.add_argument("size",type=int,choices=[0,30,60])
    args=parser.parse_args()
    for line in (HERE.parent/"n30/SHA256SUMS").read_text().splitlines():
        digest,name=line.split()
        assert hashlib.sha256((folder(30)/name).read_bytes()).hexdigest()==digest
    torch.set_num_threads(4)
    torch.manual_seed(42)
    dest=folder(31)/str(args.size)
    dest.mkdir(exist_ok=True)
    if args.action=="train":
        assert args.size in (30,60)
        train(args.size,dest)
    else:
        evaluate(args.size,dest)
