"""Check on-disk parameter changes and compare the paired first answers."""
from pathlib import Path
from collections import Counter
from safetensors import safe_open
import torch
import json
out=Path(Path(".state/n29.path").read_text().strip())
torch.set_num_threads(2)
paths=[Path(".state/n25/gli/model.safetensors"),out/"final/model.safetensors"]
with safe_open(str(paths[0]),framework="pt") as old, safe_open(str(paths[1]),framework="pt") as new:
    assert set(old.keys())==set(new.keys())
    changed=sum(not torch.equal(old.get_tensor(k),new.get_tensor(k)) for k in old.keys())
    assert changed
    report=dict(tensors=len(old.keys()),changed_tensors=changed)
rows={}
for arm in ["base","tuned"]:
    rs=[json.loads(s) for s in (out/f"{arm}.jsonl").read_text().splitlines()]
    assert len(rs)==16
    rows[arm]=rs
    fields=Counter()
    for r in rs:
        if r["expected"]["status"]=="ok":
            for f in ("comparison","aggregate","compare_to","required_structure"):
                fields[f]+= (r["answer"] or {}).get(f)==r["expected"][f]
    report[arm]=dict(valid=sum(r["error"] is None for r in rs),correct=sum(r["correct"] for r in rs),
                     correct_ids=[r["id"] for r in rs if r["correct"]], fields=fields,
                     stop_correct=sum(r["correct"] and r["expected"]["status"]!="ok" for r in rs))
assert [r["request"] for r in rows["base"]]==[r["request"] for r in rows["tuned"]]
print(json.dumps(report,indent=2))
