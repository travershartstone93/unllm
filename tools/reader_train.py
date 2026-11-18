#!/usr/bin/env python3
"""Train the reader further on the passages unllm finds (tools/reader_data.py),
on the CPU, then export it for ONNX Runtime.

  reader_train.py DATA.jsonl OUT_DIR [--base DIR] [--epochs 1] [--batch 16]

Uses the GPU when torch can see one (half precision there); the trained
reader still runs on the CPU in unllm.
  reader_train.py export OUT_DIR       write OUT_DIR/onnx/model.onnx + vocab.txt

Per question: up to 2 passages holding the answer, and 3 that do not
(the best-ranked first: those are the ones that fool it). 2% of the
questions are kept back to report the loss. Saves a checkpoint every 200
steps and resumes from it; shows a progress bar in the system tray;
pauses above 85 C.
"""

import argparse
import json
import os
import random
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recall import cool_down  # noqa: E402
from taskprogress import Task  # noqa: E402

BASE = os.path.expanduser("~/Downloads/unllm-models/minilm-uncased-squad2")
MAXLEN = 256


def examples(path, seed):
    rows = [json.loads(line) for line in open(path)]
    rnd = random.Random(seed)
    rnd.shuffle(rows)
    held = len(rows) // 50
    out = {"train": [], "dev": []}
    for i, r in enumerate(rows):
        pos = [p for p in r["passages"] if p["start"] >= 0][:2]
        neg = [p for p in r["passages"] if p["start"] < 0][:3]
        if not pos and not neg:
            continue
        side = "dev" if i < held else "train"
        out[side] += [(r["question"], p["context"], p["start"], p["text"]) for p in pos + neg]
    rnd.shuffle(out["train"])
    return out


def encode(tok, batch):
    enc = tok([b[0] for b in batch], [b[1] for b in batch], truncation="only_second", max_length=MAXLEN,
              padding=True, return_offsets_mapping=True, return_tensors="pt")
    starts, ends = [], []
    for i, (_, _, s0, text) in enumerate(batch):
        seq = enc.sequence_ids(i)
        offs = enc["offset_mapping"][i].tolist()
        st = en = 0  # the [CLS] token: no answer here
        if s0 >= 0:
            e0 = s0 + len(text)
            ctx = [j for j, s in enumerate(seq) if s == 1]
            a = [j for j in ctx if offs[j][0] <= s0 < offs[j][1] or (offs[j][0] >= s0 and offs[j][1] <= e0)]
            if a and offs[ctx[-1]][1] >= e0:
                st = a[0]
                en = max(j for j in ctx if offs[j][0] < e0)
        starts.append(st)
        ends.append(en)
    import torch
    enc.pop("offset_mapping")
    enc["start_positions"] = torch.tensor(starts)
    enc["end_positions"] = torch.tensor(ends)
    return enc


def train(args):
    import torch
    from transformers import AutoModelForQuestionAnswering, AutoTokenizer, get_linear_schedule_with_warmup
    torch.set_num_threads(args.threads)
    torch.manual_seed(0)
    data = examples(args.data, 0)
    tok = AutoTokenizer.from_pretrained(args.base)
    ckpt = os.path.join(args.out, "checkpoint")
    start_from = ckpt if os.path.exists(os.path.join(ckpt, "state.pt")) else args.base
    device = "cuda" if torch.cuda.is_available() else "cpu"
    model = AutoModelForQuestionAnswering.from_pretrained(start_from).to(device)
    scaler = torch.amp.GradScaler(enabled=device == "cuda")
    steps_per_epoch = len(data["train"]) // args.batch
    total = steps_per_epoch * args.epochs
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)
    sched = get_linear_schedule_with_warmup(opt, int(0.06 * total), total)
    step = 0
    if start_from == ckpt:
        state = torch.load(os.path.join(ckpt, "state.pt"), map_location=device)
        opt.load_state_dict(state["opt"])
        sched.load_state_dict(state["sched"])
        step = state["step"]
    print(f"on {device}: {len(data['train'])} training passages, {len(data['dev'])} kept back; {total} steps, from step {step}",
          flush=True)
    bar = Task("reader-train", "Reader training", total=total)
    bar.skip(step)

    def dev_loss():
        model.eval()
        losses = []
        with torch.no_grad():
            for i in range(0, len(data["dev"]), 64):
                enc = encode(tok, data["dev"][i:i + 64]).to(device)
                with torch.autocast(device, enabled=device == "cuda"):
                    losses.append(model(**enc).loss.item())
        model.train()
        return sum(losses) / max(1, len(losses))

    def save():
        os.makedirs(ckpt, exist_ok=True)
        model.save_pretrained(ckpt)
        tok.save_pretrained(ckpt)
        torch.save({"opt": opt.state_dict(), "sched": sched.state_dict(), "step": step, "data": args.data},
                   os.path.join(ckpt, "state.pt"))

    print(f"step {step}: kept-back loss {dev_loss():.3f}", flush=True)
    model.train()
    running = []
    while step < total:
        i = (step % steps_per_epoch) * args.batch
        batch = data["train"][i:i + args.batch]
        with torch.autocast(device, enabled=device == "cuda"):
            loss = model(**encode(tok, batch).to(device)).loss
        scaler.scale(loss).backward()
        scaler.unscale_(opt)
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        scaler.step(opt)
        scaler.update()
        sched.step()
        opt.zero_grad()
        step += 1
        running.append(loss.item())
        bar.step(detail=f"loss {sum(running[-50:]) / len(running[-50:]):.3f}")
        if step % 200 == 0 or step == total:
            save()
            if step % 1000 == 0 or step == total:
                print(f"step {step}: train loss {sum(running[-200:]) / len(running[-200:]):.3f}, "
                      f"kept-back loss {dev_loss():.3f}  {time.strftime('%H:%M')}", flush=True)
            cool_down()
    final = os.path.join(args.out, "final")
    model.save_pretrained(final)
    tok.save_pretrained(final)
    bar.finish()


def export(out):
    import torch
    from transformers import AutoModelForQuestionAnswering, AutoTokenizer
    src = os.path.join(out, "final")
    model = AutoModelForQuestionAnswering.from_pretrained(src).eval()
    tok = AutoTokenizer.from_pretrained(src)
    dst = os.path.join(out, "onnx")
    os.makedirs(dst, exist_ok=True)
    enc = tok(["who wrote it"], ["It was written by someone."], return_tensors="pt")
    names = ["input_ids", "attention_mask", "token_type_ids"]
    axes = {n: {0: "batch_size", 1: "sequence_length"} for n in names + ["start_logits", "end_logits"]}
    torch.onnx.export(model, tuple(enc[n] for n in names), os.path.join(dst, "model.onnx"), input_names=names,
                      output_names=["start_logits", "end_logits"], dynamic_axes=axes, opset_version=14,
                      dynamo=False)
    # Training leaves the vocabulary as it was; newer tokenizers save only tokenizer.json.
    vocab = os.path.join(src, "vocab.txt")
    shutil.copy(vocab if os.path.exists(vocab) else os.path.join(BASE, "vocab.txt"), os.path.join(dst, "vocab.txt"))
    print("wrote", dst)


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "export":
        return export(sys.argv[2])
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data")
    ap.add_argument("out")
    ap.add_argument("--base", default=BASE)
    ap.add_argument("--epochs", type=int, default=1)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--lr", type=float, default=3e-5)
    ap.add_argument("--threads", type=int, default=8)
    train(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())
