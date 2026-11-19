"""Fetch the pinned public checkpoints, checking each weight file against its upstream digest."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import time

import requests

MODELS = {
    "flan": ("google/flan-t5-base", "7bcac572ce56db69c1ea7c8af255c5d7c9672fc2"),
    "mt0": ("bigscience/mt0-small", "8116a34237e19160ec003147e758f065876d95f0"),
    "smol": ("HuggingFaceTB/SmolLM2-360M-Instruct", "a10cc1512eabd3dde888204e902eca88bddb4951"),
    "qwen": ("Qwen/Qwen2.5-0.5B-Instruct", "7ae557604adf67be50417f59c2c2f167def9a775"),
    "coder": ("Qwen/Qwen2.5-Coder-1.5B-Instruct", "2e1fd397ee46e1388853d2af2c993145b0f1098a"),
}
OUT = Path(Path(".state/n32.path").read_text().strip())
def download(key):
    repo,rev=MODELS[key]
    folder=OUT/"models"/key
    folder.mkdir(parents=True,exist_ok=True)
    base=f"https://huggingface.co/{repo}/resolve/{rev}/"
    resp=requests.get(f"https://huggingface.co/api/models/{repo}/revision/{rev}",params={"blobs":"true"}, timeout=40)
    resp.raise_for_status()
    metadata=resp.json()
    for info in sorted(metadata["siblings"],key=lambda r:r["rfilename"]=="model.safetensors"):
        name=info["rfilename"]
        if name not in {"config.json","generation_config.json","model.safetensors","tokenizer.json","vocab.json","merges.txt","special_tokens_map.json","spiece.model","tokenizer_config.json","LICENSE","README.md"}: continue
        dest=folder/name
        if dest.exists() and dest.stat().st_size==info["size"]:continue
        for attempt in range(4):
            try:
                with requests.get(base+name,params={"download":"true","n32":time.time_ns()},stream=True,timeout=(20,100)) as r:
                    r.raise_for_status()
                    h=hashlib.sha256()
                    with dest.with_suffix(".part").open("wb") as out:
                        for chunk in r.iter_content(2**20):
                            h.update(chunk);out.write(chunk)
                    assert dest.with_suffix(".part").stat().st_size==info["size"],name
                    if name=="model.safetensors":assert h.hexdigest()==info["lfs"]["sha256"]
                    dest.with_suffix(".part").replace(dest)
                break
            except Exception as exc:
                print(key,name,repr(exc),flush=True)
                if attempt==3:raise
    (folder/"pinned.json").write_text(json.dumps({"repo":repo,"revision":rev,"weight":next(i["lfs"] for i in metadata["siblings"] if i["rfilename"]=="model.safetensors")}))
    print(key,"ready",flush=True)
if __name__=="__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:list(pool.map(download,MODELS))
