"""Public-only GLiNER2 fine-tune with the fixed N25 extraction interface."""
from pathlib import Path
import sys,json,re,random
import torch
from gliner2 import GLiNER2
from gliner2.training.trainer import GLiNER2Trainer, TrainingConfig
from gliner2.training.data import InputExample, Classification
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"n25"))
import evaluate as e

def location(n):
    return Path(Path(f".state/n{n}.path").read_text().strip())
def read(n,file):
    return list(map(json.loads,(location(n)/file).read_text().splitlines()))
out=location(29)
base=e.ASSETS/"gli"
torch.set_num_threads(4)
torch.manual_seed(42)
random.seed(42)

def evaluate(path,arm):
    model=GLiNER2.from_pretrained(str(base)).cpu().eval()
    if path != base:
        from safetensors.torch import load_file
        model.load_state_dict(load_file(str(path/"model.safetensors")),strict=True)
    schema=model.create_schema().entities({e.LITERAL:"The fixed integer used as the actual comparison threshold"})
    for key,labels in e.LABELS.items():
        schema.classification(key,list(labels))
    with torch.inference_mode(), (out/f"{arm}.jsonl").open("w") as f:
        for r in read(26,"holdout.jsonl"):
            raw=model.extract(r["request"],schema)
            spans=raw.get("entities",{}).get(e.LITERAL,[])
            answer=None
            error=None
            try:
                answer=e.normalized(dict(raw)|{e.LITERAL:spans[0] if len(spans)==1 else None})
                e.jsonschema.validate(answer,e.SCHEMA)
            except (KeyError,ValueError,TypeError,e.jsonschema.ValidationError) as ex:
                error=str(ex).splitlines()[0]
            expected=r["expected"]
            if expected["status"]=="clarify":
                key={"h13":"comparison direction","h14":"aggregation"}[r["id"]]
                expected=dict(status="clarify",question=e.LABELS["missing information if clarification is needed"][key])
            print(json.dumps(dict(**r,raw=raw,answer=answer,error=error,correct=error is None and answer==expected)),file=f)

def train():
    pairs={r["request"]:r["requirements"] for r in read(26,"direct.jsonl")}
    bound={r["row"]:r for r in read(27,"contextual.jsonl")}
    for r in read(27,"reviewed-fields.jsonl"):
        s=r["source"]; b=bound.get(s["row"])
        text=s["question"] if b is None else b["binding"]+" "+s["question"]
        pairs[text]=r["reviewed_fields"] if b is None else b["requirements"]
    examples=[]
    for text,fields in pairs.items():
        tags=[]
        mapping={"aggregate":"aggregation","comparison":"each element compared to threshold","required_structure":"explicitly required implementation","collection":"collection parameter","status":"request outcome"}
        def add(task,gold):
            labels=e.LABELS[task]
            tags.append(Classification(task,list(labels),next(k for k,v in labels.items() if v==gold)))
        for key,task in mapping.items():
            if key in fields: add(task,fields[key])
        ent={}
        target=fields.get("compare_to",{})
        if target:
            add("threshold source",next(iter(target)))
            if "literal" in target:
                literals=[m.group() for m in re.finditer(r"(?<![\w.])-?\d[\d,]*(?:\.\d+)?(?!\w)",text) if float(m.group().replace(",",""))==target["literal"]]
                if len(literals)==1: ent[e.LITERAL]=literals
            else: ent[e.LITERAL]=[]
        examples.append(InputExample(text=text,classifications=tags,entities=ent))
    assert len(examples)==110
    model=GLiNER2.from_pretrained(str(base))
    config=TrainingConfig(output_dir=str(out),num_epochs=2,batch_size=2,encoder_lr=1e-5,task_lr=5e-5,fp16=False,bf16=False,num_workers=0,save_best=False,logging_steps=10,seed=42)
    trainer=GLiNER2Trainer(model,config)
    result=trainer.train(train_data=examples)
    (out/"training.json").write_text(json.dumps(result,default=str))
if sys.argv[1]=="train": train()
else: evaluate(Path(sys.argv[2]) if len(sys.argv)>2 else base,sys.argv[1])
