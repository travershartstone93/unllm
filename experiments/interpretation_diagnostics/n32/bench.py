"""Evaluate local pretrained interpreters; optionally fine-tune the selected checkpoint."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import random
import re
import sys
import time

import torch
from transformers import LogitsProcessor, AutoConfig, AutoModelForCausalLM, AutoModelForSeq2SeqLM, AutoTokenizer
from lmformatenforcer import RegexParser
from lmformatenforcer.integrations.transformers import build_token_enforcer_tokenizer_data, build_transformers_prefix_allowed_tokens_fn
from fetch import MODELS, OUT

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0,str(HERE.parent/"qwen_contract"))
from trial import SCHEMA, jsonschema

PROMPT = """Interpret a coding request for a function with values (a list of integers) and cutoff (an integer argument). Output one line.
For a supported request output four words: OP COMPARISON THRESHOLD STRUCTURE.
OP is count or sum. COMPARISON is above, atleast, below, atmost, or equal. Compare each list entry to the threshold. THRESHOLD is cutoff if supplied by the caller, otherwise the signed integer in decimal digits. STRUCTURE is any unless explicitly required: use for for a required for loop, or nested for an if inside a for loop.
If an essential choice is missing output clarify followed by boundary (include equality?), direction (which side?), threshold (which value?), or aggregate (count or sum?). For a clear request outside these operations output unsupported.
Request: """
OPS={"above":"gt","atleast":"ge","below":"lt","atmost":"le","equal":"eq"}
STRUCT={"any":[],"for":["for"],"nested":["for","if"]}
QUESTIONS={"boundary":"Should values equal to the boundary qualify?","direction":"Which side of the cutoff should qualify?",
           "threshold":"What threshold should be used?","aggregate":"Should I count or sum?"}
TYPES={"boundary":("t11a","t18a","t23a","d21"),"direction":("t15b","h13"),
       "threshold":("t14b","t19a","t26a","t30b"),"aggregate":("t22b","t27b","d22","h14")}
GRAMMAR=r" ?((count|sum) (above|atleast|below|atmost|equal) (cutoff|-?(0|[1-9][0-9]{0,18})) (any|for|nested)|clarify (boundary|direction|threshold|aggregate)|unsupported)"
def dataset(n,split):
    d=Path((ROOT/f".state/n{n}.path").read_text().strip())
    return [json.loads(s) for s in (d/f"{split}.jsonl").read_text().splitlines()]

def encode_gold(row):
    e=row["expected"]
    if e["status"]=="unsupported":
        return "unsupported"
    if e["status"]=="clarify":
        return "clarify "+next(k for k,ids in TYPES.items() if row["id"] in ids)
    cmp=next(k for k,v in OPS.items() if v==e["comparison"])
    structure=next(k for k,v in STRUCT.items() if v==e["required_structure"])
    return f"{e['aggregate']} {cmp} {e['compare_to'].get('literal','cutoff')} {structure}"

def parse(text):
    text=text.strip()
    if not re.fullmatch(GRAMMAR,text):
        return None
    if text=="unsupported":
        return {"status":"unsupported"}
    parts=text.split()
    if parts[0]=="clarify":
        return {"status":"clarify","question":QUESTIONS[parts[1]]}
    agg,cmp,k,structure=parts
    if k!="cutoff" and not -(2**63)<=int(k)<2**63:
        return None
    return dict(status="ok",aggregate=agg,collection="values",comparison=OPS[cmp],
                compare_to={"input":"cutoff"} if k=="cutoff" else {"literal":int(k)},required_structure=STRUCT[structure])

class TokenView:
    """Use a plain zero token for LMFE's whitespace probe, also on EOS-appending T5 tokenizers."""
    def __init__(self,tok):
        self.tok=tok
        self.all_special_ids=set(tok.all_special_ids)
        self.eos_token_id=tok.eos_token_id
    def encode(self,text):
        return self.tok.encode(text,add_special_tokens=False)
    def decode(self,*args,**kwargs):
        return self.tok.decode(*args,clean_up_tokenization_spaces=False,**kwargs)
    def __len__(self):
        return len(self.tok)

class FiniteScores(LogitsProcessor):
    def __call__(self, input_ids, scores):
        # Grammar masking may legitimately set many logits to negative infinity.
        if torch.isnan(scores).any() or torch.isposinf(scores).any() or not torch.isfinite(scores).any(dim=1).all():
            raise FloatingPointError("Invalid scores in model generation")
        return scores

def load(key,adapter=None,training=False):
    folder=OUT/"models"/key
    seq=AutoConfig.from_pretrained(folder).is_encoder_decoder
    tok=AutoTokenizer.from_pretrained(folder)
    tok.padding_side="right" if seq else "left"
    if tok.pad_token_id is None:tok.pad_token=tok.eos_token
    cls=AutoModelForSeq2SeqLM if seq else AutoModelForCausalLM
    dtype=torch.float16 if training and key=="coder" else torch.float32
    model=cls.from_pretrained(folder,dtype=dtype,device_map={"":"cuda"},attn_implementation="sdpa" if not seq else "eager").eval()
    if adapter:
        from peft import PeftModel
        model=PeftModel.from_pretrained(model,adapter).eval()
    model.generation_config.do_sample=False
    model.generation_config.num_beams=1
    model.generation_config.repetition_penalty=1.
    model.generation_config.no_repeat_ngram_size=0
    return tok,model,seq

def prompt(tok,seq,row):
    text=PROMPT+row["request"]
    if seq:return text
    return tok.apply_chat_template([{"role":"user","content":text}],tokenize=False,add_generation_prompt=True)

def evaluate(key,dest,adapter=None):
    tok,model,seq=load(key,adapter)
    indexed=build_token_enforcer_tokenizer_data(TokenView(tok))
    def constraint():
        return build_transformers_prefix_allowed_tokens_fn(indexed,RegexParser(GRAMMAR))
    # Tokenizer/grammar reachability on invented complete output strings, independent of test meanings.
    for witness in ["count above cutoff any","sum atmost -273 nested","clarify boundary","unsupported"]:
        ids=torch.tensor([tok.pad_token_id]); fn=constraint()
        for t in tok.encode(witness,add_special_tokens=False)+[tok.eos_token_id]:
            assert t in fn(0,ids),(key,witness,t,tok.decode(ids))
            ids=torch.cat([ids,torch.tensor([t])])
    summary={"model":key,"parameters":sum(p.numel() for p in model.parameters())}
    batch_size=1 if key=="coder" else 4
    splits=[("dev",dataset(30,"dev")),("regression",dataset(26,"holdout"))]
    if adapter:
        splits.append(("training_fit",dataset(30,"train")))
    with torch.inference_mode():
        for split,rows in splits:
            for mode in (["constrained"] if split=="training_fit" else ["plain","constrained"]):
                output=[]
                start=time.monotonic()
                for offset in range(0,len(rows),batch_size):
                    batch=rows[offset:offset+batch_size]
                    encoded=tok([prompt(tok,seq,r) for r in batch],padding=True,return_tensors="pt",add_special_tokens=seq).to("cuda")
                    kwargs={"prefix_allowed_tokens_fn":constraint()} if mode=="constrained" else {}
                    ids=model.generate(**encoded,max_new_tokens=48,do_sample=False,num_beams=1,pad_token_id=tok.pad_token_id,logits_processor=[FiniteScores()],**kwargs)
                    suffix=ids[:,1:] if seq else ids[:,encoded.input_ids.shape[1]:]
                    for row,raw in zip(batch,tok.batch_decode(suffix,skip_special_tokens=True,clean_up_tokenization_spaces=False)):
                        answer=parse(raw)
                        if answer is not None:jsonschema.validate(answer,SCHEMA)
                        target=parse(encode_gold(row))
                        if row["expected"]["status"]!="clarify": assert target==row["expected"]
                        output.append(dict(**row,raw=raw,answer=answer,correct=answer==target))
                (dest/f"{split}-{mode}.jsonl").write_text("".join(json.dumps(x)+"\n" for x in output))
                score={"total":len(output),"valid":sum(r["answer"] is not None for r in output),"exact":sum(r["correct"] for r in output),
                       "by_status":dict(Counter(r["expected"]["status"] for r in output if r["correct"])),
                       "seconds":round(time.monotonic()-start,2),"correct_ids":[r["id"] for r in output if r["correct"]]}
                summary[f"{split}-{mode}"]=score
                print(key,split,mode,score,flush=True)
    (dest/"scores.json").write_text(json.dumps(summary,indent=2))

def train(key,dest):
    from peft import LoraConfig, get_peft_model
    tok,model,seq=load(key,training=True)
    model.config.use_cache=False
    model=get_peft_model(model,LoraConfig(r=8,lora_alpha=16,lora_dropout=0.05,target_modules=["q","v"] if seq else ["q_proj","v_proj"],
                                          task_type="SEQ_2_SEQ_LM" if seq else "CAUSAL_LM"))
    model.train()
    data=[]
    for row in dataset(30,"train"):
        prompt_ids=tok.encode(prompt(tok,seq,row),add_special_tokens=seq)
        targets=tok.encode(encode_gold(row),add_special_tokens=False)+[tok.eos_token_id]
        if seq:data.append((prompt_ids,targets))
        else:data.append((prompt_ids+targets,[-100]*len(prompt_ids)+targets))
    def right_pad(values,pad):
        width=max(map(len,values))
        return torch.tensor([v+[pad]*(width-len(v)) for v in values],device="cuda")
    optimizer=torch.optim.AdamW([p for p in model.parameters() if p.requires_grad],lr=0.0003)
    scaler=torch.amp.GradScaler("cuda",enabled=key=="coder")
    rng=random.Random(42)
    losses=[]
    applied=0
    # Keep effective batch four. The larger causal model needs smaller GPU microbatches.
    micro=1 if not seq and sum(p.numel() for p in model.parameters())>10**9 else 4
    for epoch in range(5):
        rng.shuffle(data); epoch_loss=0
        for i in range(0,len(data),4):
            optimizer.zero_grad(set_to_none=True)
            batch=data[i:i+4]
            batch_tokens=sum(sum(v!=-100 for v in ys) for _,ys in batch)
            for j in range(0,len(batch),micro):
                xs,ys=zip(*batch[j:j+micro])
                token_count=sum(sum(v!=-100 for v in labels) for labels in ys)
                ids=right_pad(xs,tok.pad_token_id)
                with torch.autocast("cuda",dtype=torch.float16,enabled=key=="coder"):
                    raw=model(input_ids=ids,attention_mask=right_pad([[1]*len(x) for x in xs],0),labels=right_pad(ys,-100)).loss
                    loss=raw*(token_count/batch_tokens)
                assert torch.isfinite(loss)
                scaler.scale(loss).backward()
                epoch_loss+=loss.item()
            scaler.unscale_(optimizer)
            torch.nn.utils.clip_grad_norm_([p for p in model.parameters() if p.requires_grad],1.0)
            scale=scaler.get_scale()
            scaler.step(optimizer);scaler.update()
            applied+=int(scaler.get_scale()>=scale)
        losses.append(epoch_loss/15);print(key,"epoch",epoch+1,losses[-1],flush=True)
    assert applied>0 and any(v.detach().count_nonzero().item()>0 for k,v in model.named_parameters() if "lora_B." in k)
    model.save_pretrained(dest/"adapter")
    (dest/"training.json").write_text(json.dumps(dict(rows=60,epochs=5,batches=75,optimizer_steps=applied,microbatch=micro,seed=42,losses=losses,trainable=model.get_nb_trainable_parameters()[0]),indent=2))

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("action",choices=["vanilla","train","trained"])
    parser.add_argument("key",choices=MODELS)
    args=parser.parse_args()
    torch.manual_seed(42);torch.set_num_threads(3)
    dest=OUT/("vanilla" if args.action=="vanilla" else "finetuned")/args.key
    dest.mkdir(parents=True,exist_ok=True)
    if args.action=="train":train(args.key,dest)
    else:evaluate(args.key,dest,dest/"adapter" if args.action=="trained" else None)
