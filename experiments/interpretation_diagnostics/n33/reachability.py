"""Check whether the N32 grammar admits the tokenized reference answer for every development row."""
from pathlib import Path
import sys,json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"n32"))
import bench as b
out=Path(Path(".state/n33.path").read_text().strip())
results={}
for key in b.MODELS:
    folder=b.OUT/"models"/key
    tok=b.AutoTokenizer.from_pretrained(folder)
    config=b.AutoConfig.from_pretrained(folder)
    indexed=b.build_token_enforcer_tokenizer_data(b.TokenView(tok))
    parser=b.RegexParser(b.GRAMMAR)
    failures=[]
    for row in b.dataset(30,"dev"):
        fn=b.build_transformers_prefix_allowed_tokens_fn(indexed,parser)
        prefix=([config.decoder_start_token_id] if config.is_encoder_decoder else
                tok.encode(b.prompt(tok,False,row),add_special_tokens=False))
        for t in tok.encode(b.encode_gold(row),add_special_tokens=False)+[tok.eos_token_id]:
            if t not in fn(0,b.torch.tensor(prefix)):
                failures.append(row["id"])
                break
            prefix.append(t)
    results[key]=dict(total=24,failed=failures)
    (out/"reachability.json").write_text(json.dumps(results,indent=2))
