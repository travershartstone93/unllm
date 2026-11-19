"""Frozen local extraction adapters for the ten N21 requests."""
import argparse
import json
import sys
import time
from pathlib import Path

import torch
from text_to_num import text2num

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ASSETS = ROOT / ".state/n25"
sys.path.insert(0, str(HERE.parent / "qwen_contract"))
from trial import CASES, SCHEMA, jsonschema

# Known signature as in N21. Labels are fixed for every sentence.
LABELS = {
    "request outcome": {"construct a count or sum":"ok", "ask for missing information":"clarify",
                        "clear request outside count and sum":"unsupported"},
    "aggregation": {"count entries":"count", "sum entries":"sum"},
    "collection parameter": {"values":"values", "other collection":"other"},
    "each element compared to threshold": {"greater than":"gt", "greater than or equal to":"ge",
        "less than":"lt", "less than or equal to":"le", "equal to":"eq"},
    "threshold source": {"caller supplied cutoff parameter":"input", "fixed integer literal":"literal"},
    "explicitly required implementation": {"unspecified implementation":[], "for loop":["for"],
                                           "if nested inside for loop":["for","if"]},
    "missing information if clarification is needed": {
        "comparison direction":"Which side of the cutoff should qualify?",
        "threshold value":"What threshold should be used?", "aggregation":"Should I count or sum?",
        "none":None},
}
LITERAL = "fixed threshold integer"
TEMPLATE = {k:list(v) for k,v in LABELS.items()} | {LITERAL:"integer"}


def normalized(raw):
    def field(k):
        return LABELS[k][raw[k]]
    status = field("request outcome")
    if status == "unsupported":
        return dict(status=status)
    if status == "clarify":
        return dict(status=status, question=field("missing information if clarification is needed"))
    threshold = {"input":"cutoff"}
    if field("threshold source") == "literal":
        number = raw[LITERAL]
        if isinstance(number, str):
            try:
                number = int(number)
            except ValueError:
                number = text2num(number,"en")
        if type(number) is not int:
            raise ValueError("expected a single fixed integer")
        threshold = {"literal":number}
    return dict(status=status, aggregate=field("aggregation"), collection=field("collection parameter"),
                comparison=field("each element compared to threshold"), compare_to=threshold,
                required_structure=field("explicitly required implementation"))


def gliner():
    from gliner2 import GLiNER2
    model = GLiNER2.from_pretrained(str(ASSETS/"gli")).to("cpu").eval()
    schema = model.create_schema().entities({LITERAL:"The fixed integer used as the actual comparison threshold"})
    for key, labels in LABELS.items():
        schema.classification(key,list(labels))
    check = model.extract("Apple was founded by Steve Jobs.",model.create_schema().entities(["person"]))
    assert "Steve Jobs" in check["entities"]["person"], check
    def infer(prompt):
        raw = model.extract(prompt,schema)
        nums = raw["entities"][LITERAL]
        flat = dict(raw)
        flat[LITERAL] = nums[0] if len(nums)==1 else None
        return raw, flat
    return infer, check


def nuextract():
    from transformers import AutoTokenizer, Qwen2VLForConditionalGeneration, StoppingCriteria, StoppingCriteriaList
    folder = ASSETS/"nu"
    tokenizer = AutoTokenizer.from_pretrained(str(folder))
    template = json.loads((folder/"chat_template.json").read_text())["chat_template"]
    model = Qwen2VLForConditionalGeneration.from_pretrained(str(folder),dtype=torch.float16,
                    attn_implementation="sdpa").to("cuda").eval()
    def generate(prompt,schema):
        encoded = tokenizer.apply_chat_template([dict(role="user",content=prompt)],chat_template=template,
                template=json.dumps(schema),add_generation_prompt=True,return_tensors="pt").to("cuda")
        size = encoded.shape[1]
        class LoopGuard(StoppingCriteria):
            def __call__(self,input_ids,scores,**kwargs):
                text=tokenizer.decode(input_ids[0,size:],skip_special_tokens=True)
                return text.lower().count("but wait")>=3 or (len(text)>200 and text[-64:]*3 in text)
        output = model.generate(encoded,attention_mask=torch.ones_like(encoded),max_new_tokens=256,
                    do_sample=False,max_time=45,stopping_criteria=StoppingCriteriaList([LoopGuard()]))
        return tokenizer.decode(output[0,size:],skip_special_tokens=True)
    control = generate("My name is Susan Smith.", {"name":"verbatim-string"})
    assert json.loads(control)["name"] == "Susan Smith", control
    def infer(prompt):
        raw = generate(prompt,TEMPLATE)
        try:
            return raw, json.loads(raw)
        except ValueError:
            return raw, {}
    return infer, control


def main(arm):
    torch.set_num_threads(4)
    torch.manual_seed(42)
    with torch.inference_mode():
        infer, control = {"gliner":gliner,"nuextract":nuextract}[arm]()
        (HERE/(arm+"-control.json")).write_text(json.dumps(control,indent=2)+"\n")
        with (HERE/(arm+".jsonl")).open("w") as out:
            for i,(prompt,agg,cmp) in enumerate(CASES,1):
                start=time.monotonic()
                raw, flat = infer(prompt)
                row=dict(case=i,prompt=prompt,raw=raw,seconds=round(time.monotonic()-start,3),valid=False,correct=False)
                try:
                    answer=normalized(flat)
                    row["answer"]=answer
                    jsonschema.validate(answer,SCHEMA)
                    row["valid"]=True
                    if i==9:
                        row["correct"]=answer==dict(status="clarify",question=LABELS["missing information if clarification is needed"]["comparison direction"])
                    elif i==10:
                        row["correct"]=answer==dict(status="unsupported")
                    else:
                        row["correct"]=answer==dict(status="ok",aggregate=agg,collection="values",comparison=cmp,
                                             compare_to={"literal":5} if i==6 else {"input":"cutoff"},
                                             required_structure=["for","if"] if i==8 else [])
                except (KeyError,TypeError,ValueError,jsonschema.ValidationError) as error:
                    row["error"]=str(error).splitlines()[0]
                print(json.dumps(row),file=out,flush=True)
                print(arm,i,row["valid"],row["correct"],flush=True)


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("arm",choices=["gliner","nuextract"])
    main(parser.parse_args().arm)
