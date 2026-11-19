"""Minimal CPU reference for the two zero-scoring T5-family models, following publisher cards."""
from pathlib import Path
import json
import torch
from transformers import T5Tokenizer, T5ForConditionalGeneration, AutoTokenizer, AutoModelForSeq2SeqLM

torch.set_num_threads(3)
models=Path(Path(".state/n32.path").read_text().strip())/"models"
out=Path(Path(".state/n33.path").read_text().strip())
for key in ("flan","mt0"):
    if key=="flan":
        tok=T5Tokenizer.from_pretrained(models/key)
        model=T5ForConditionalGeneration.from_pretrained(models/key).eval()
    else:
        tok=AutoTokenizer.from_pretrained(models/key)
        model=AutoModelForSeq2SeqLM.from_pretrained(models/key).eval()
    with torch.inference_mode(), (out/f"{key}-cpu-reference.jsonl").open("w") as f:
        for text in ["translate English to German: How old are you?","Translate to English: Je t’aime.","What is the capital of France?","Repeat exactly: sum below -3 nested"]:
            ids=tok.encode(text,return_tensors="pt")
            reply=model.generate(ids)
            print(json.dumps(dict(prompt=text,raw=tok.decode(reply[0],skip_special_tokens=True))),file=f,flush=True)
    del model
