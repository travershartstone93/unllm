"""T5-family zero-shot classification via candidate likelihoods, with separate literal generation."""
from pathlib import Path
import json
import re
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"n32"))
import bench as b
import torch

OUT=Path(Path(".state/n33.path").read_text().strip())
TASKS={
    "status":("Can a function that counts or sums matching integers satisfy this request, or should it ask for clarification or decline?",
              {"ok":"It can satisfy the request.","clarify":"It must ask for clarification.","unsupported":"The request is outside its capabilities."}),
    "aggregate":("What result is being requested?",{"count":"The number of qualifying entries.","sum":"The sum of the qualifying values."}),
    "comparison":("Where is a qualifying list entry relative to the threshold?",
                  {"gt":"Greater than the threshold.","ge":"Greater than or equal to the threshold.","lt":"Less than the threshold.",
                   "le":"Less than or equal to the threshold.","eq":"Equal to the threshold."}),
    "source":("Where does the comparison threshold come from?",
              {"input":"The caller supplies the cutoff.","literal":"The request fixes a specific number."}),
    "structure":("Which implementation does the user explicitly require?",
                 {"any":"No particular loop is required.","for":"A for loop is required.","nested":"An if inside a for loop is required."}),
    "missing":("Which decision does the question leave open?",{"boundary":"Whether to include ties.","direction":"Which side qualifies.",
                                                              "threshold":"Which threshold to use.","aggregate":"Whether to count or sum."})
}

def run(key):
    tok,model,_=b.load(key)
    rows=b.dataset(30,"dev")
    predictions=[{} for _ in rows]
    with torch.inference_mode():
        for field,(question,options) in TASKS.items():
            targets=tok(list(options.values()),padding=True,return_tensors="pt").input_ids.to("cuda")
            targets=targets.masked_fill(targets==tok.pad_token_id,-100)
            for i in range(0,len(rows),4):
                excerpts=[r["request"] for r in rows[i:i+4]]
                inp=tok([f"{text}\nQuestion: {question}\nAnswer:" for text in excerpts],return_tensors="pt",padding=True).to("cuda")
                repeated={k:v.repeat_interleave(len(options),dim=0) for k,v in inp.items()}
                labels=targets.repeat(len(excerpts),1)
                logits=model(**repeated,labels=labels,use_cache=False).logits
                nll=torch.nn.functional.cross_entropy(logits.transpose(1,2),labels,reduction="none")
                scores=(-nll.sum(1)/labels.ne(-100).sum(1)).reshape(len(excerpts),len(options))
                assert torch.isfinite(scores).all()
                for j,idx in enumerate(scores.argmax(1).tolist()):
                    predictions[i+j][field]=list(options)[idx]
        results=[]
        for row,p in zip(rows,predictions):
            integer_text=None
            if p["status"]=="clarify":
                answer=dict(status="clarify",question=b.QUESTIONS[p["missing"]])
            elif p["status"]=="unsupported":
                answer=dict(status="unsupported")
            else:
                threshold={"input":"cutoff"}
                if p["source"]=="literal":
                    inp=tok(row["request"]+"\nWhat fixed threshold is used? Answer with a signed integer in digits.",return_tensors="pt").to("cuda")
                    out=model.generate(**inp,max_new_tokens=20,do_sample=False,num_beams=1,logits_processor=[b.FiniteScores()])
                    integer_text=tok.decode(out[0],skip_special_tokens=True).strip()
                    threshold={"literal":int(integer_text)} if re.fullmatch(r"[+-]?[0-9]{1,19}",integer_text) and -(2**63)<=int(integer_text)<2**63 else None
                answer=dict(status="ok",collection="values",aggregate=p["aggregate"],comparison=p["comparison"],
                            compare_to=threshold,required_structure=b.STRUCT[p["structure"]]) if threshold else None
            if answer is not None:b.jsonschema.validate(answer,b.SCHEMA)
            results.append(dict(**row,fields=p,integer_text=integer_text,answer=answer,correct=answer==b.parse(b.encode_gold(row))))
    (OUT/f"{key}-fields.jsonl").write_text("".join(json.dumps(r)+"\n" for r in results))
    print(key,sum(r["correct"] for r in results),"/",len(results))
if __name__=="__main__":
    torch.set_num_threads(3)
    for name in ("flan","mt0"):run(name)
