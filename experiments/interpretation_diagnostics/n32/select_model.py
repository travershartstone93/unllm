"""Verify the five vanilla score files and apply the preregistered selection rule."""
import json
from fetch import MODELS, OUT

def rank(s,mode):
    result=s["dev-"+mode]
    by=result["by_status"]
    return (result["exact"],by.get("clarify",0)+by.get("unsupported",0),-s["parameters"])

def checked(path):
    s=json.loads((path/"scores.json").read_text())
    for split,n in (("dev",24),("regression",16)):
        for mode in ("plain","constrained"):
            name=f"{split}-{mode}"
            rows=[json.loads(r) for r in (path/f"{name}.jsonl").read_text().splitlines()]
            result=s[name]
            assert n==len(rows)==len({r["id"] for r in rows})==result["total"]
            assert sum(r["correct"] for r in rows)==result["exact"]
            assert sum(r["answer"] is not None for r in rows)==result["valid"]
    return s

if __name__=="__main__":
    candidates=[checked(OUT/"vanilla"/key) for key in MODELS]
    selection={}
    for mode in ("plain","constrained"):
        order=sorted(candidates,key=lambda s:rank(s,mode),reverse=True)
        selection[mode]=[s["model"] for s in order]
    (OUT/"selection.json").write_text(json.dumps(selection,indent=2))
    print(selection["constrained"][0])
