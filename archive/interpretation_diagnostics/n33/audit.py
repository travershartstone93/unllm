"""Audit the N32 wrappers, then score controls, relation-only prompts, and four-shot interpretation."""
from collections import Counter
from pathlib import Path
import json
import re
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"n32"))
import bench as b
import torch

OUT=Path(Path(".state/n33.path").read_text().strip())
REL={"greater than":"gt","at least":"ge","less than":"lt","at most":"le","equal to":"eq"}
CONTROLS=[("capital","What is the capital of France?","paris"),
          ("translate","Translate to English: Je t’aime.","i love you"),
          ("copy","Repeat exactly: sum below -3 nested","sum below -3 nested")]

def audit(key):
    tok,model,seq=b.load(key)
    idx=b.build_token_enforcer_tokenizer_data(b.TokenView(tok))
    rows=b.dataset(30,"dev")
    train={r["id"]:r for r in b.dataset(30,"train")}
    examples=[train[i] for i in ("t03a","t17a","t18a","t17b")]
    assert {r["request"] for r in examples}.isdisjoint(r["request"] for r in rows)
    intro=b.PROMPT.rsplit("Request: ",1)[0]
    intro+="".join(f'Request: {r["request"]}\nAnswer: {b.encode_gold(r)}\n' for r in examples)
    tasks={"controls":[(i,p,t) for i,p,t in CONTROLS],
           "comparison":[(r["id"],f'Request: {r["request"]}\nHow does a qualifying entry compare to the threshold? Choose one: greater than, at least, less than, at most, equal to. Give only the chosen phrase.',r["expected"]["comparison"]) for r in rows if r["expected"]["status"]=="ok"]}
    for mode in ("plain","constrained"):
        tasks[mode]=[(r["id"],intro+"Request: "+r["request"]+"\nAnswer:",b.encode_gold(r)) for r in rows]
    stats={}
    for arm,items in tasks.items():
        output=[]
        size=1 if key=="coder" else 4
        for offset in range(0,len(items),size):
            batch=items[offset:offset+size]
            texts=[]
            for _,content,_ in batch:
                messages=[dict(role="user",content=content)]
                text=content if seq else tok.apply_chat_template(messages,tokenize=False,add_generation_prompt=True)
                native=tok.encode(content) if seq else tok.apply_chat_template(messages,tokenize=True,add_generation_prompt=True)
                assert native==tok.encode(text,add_special_tokens=seq)
                texts.append(text)
            inp=tok(texts,padding=True,return_tensors="pt",add_special_tokens=seq).to("cuda")
            kwargs={}
            if arm=="constrained":
                kwargs["prefix_allowed_tokens_fn"]=b.build_transformers_prefix_allowed_tokens_fn(idx,b.RegexParser(b.GRAMMAR))
            with torch.inference_mode():
                ids=model.generate(**inp,max_new_tokens=48 if arm in ("plain","constrained") else 22,
                                   do_sample=False,num_beams=1,pad_token_id=tok.pad_token_id,logits_processor=[b.FiniteScores()],**kwargs)
            if seq:
                assert (ids[:,0]==model.config.decoder_start_token_id).all()
                new=ids[:,1:]
            else:
                width=inp.input_ids.shape[1]
                assert torch.equal(ids[:,:width],inp.input_ids)
                new=ids[:,width:]
            for (i,_,target),raw,length in zip(batch,tok.batch_decode(new,skip_special_tokens=True,clean_up_tokenization_spaces=False),inp.attention_mask.sum(1).tolist()):
                cleaned=raw.strip().lower().strip(' .!"\'\n')
                valid=True
                if arm=="controls":
                    correct=cleaned==target if i=="copy" else bool(re.search(r"\b"+target+r"\b",cleaned))
                elif arm=="comparison":
                    valid=cleaned in REL
                    correct=REL.get(cleaned)==target
                else:
                    answer=b.parse(raw)
                    valid=answer is not None
                    correct=answer==b.parse(target)
                output.append(dict(id=i,raw=raw,target=target,correct=correct,valid=valid,prompt_tokens=length))
        (OUT/f"{key}-{arm}.jsonl").write_text("".join(json.dumps(r)+"\n" for r in output))
        stats[arm]=dict(total=len(output),correct=sum(r["correct"] for r in output),valid=sum(r["valid"] for r in output),ids=[r["id"] for r in output if r["correct"]],max_input=max(r["prompt_tokens"] for r in output))
        print(key,arm,stats[arm],flush=True)
    (OUT/f"{key}-scores.json").write_text(json.dumps(stats,indent=2))
if __name__=="__main__":
    torch.set_num_threads(3)
    torch.manual_seed(42)
    audit(sys.argv[1])
