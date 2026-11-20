#!/usr/bin/env python3
"""Offline fitting only. Inference uses the native picker and the same BGE encoder."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from sklearn.linear_model import LogisticRegression
import sklearn

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
for arg in ("catalog", "reviews", "embedder", "encoder", "output"):
    parser.add_argument(arg, type=Path)
parser.add_argument("--reference-vectors", type=Path, help="save labeled embeddings for a data-matched nearest-neighbor control")
args = parser.parse_args()
catalog = json.loads(args.catalog.read_text())
ids = {card["id"] for card in catalog}
rows = {}
ambiguous = set()


def add(text, label):
    if text in rows and rows[text] != label:
        ambiguous.add(text)
    rows[text] = label


def examples(path):
    for line in path.read_text().splitlines():
        label, text = line.split("\t")
        yield text, label if label in ids else "-"


for card in catalog:
    add(card["description"], card["id"])
    for output in card.get("outputs", []):
        add(output, card["id"])
for row in json.loads(args.reviews.read_text())["examples"]:
    add(row["text"], row["brick"] or "-")
for text, label in examples(here/"examples.tsv"):
    add(text, label)
for text in ambiguous:
    del rows[text]
validation = dict(examples(here/"validation.tsv"))
if rows.keys() & validation.keys():
    raise ValueError("validation texts overlap training")
inputs = list(rows) + list(validation)
output = subprocess.run([args.embedder, "--embed", args.encoder], input="\n".join(inputs)+"\n",
                        text=True, capture_output=True, check=True)
vectors = np.array([json.loads(line) for line in output.stdout.splitlines()])
assert vectors.shape == (len(inputs), 384)
if args.reference_vectors:
    np.savez_compressed(args.reference_vectors, vectors=vectors, texts=inputs,
                        labels=list(rows.values()) + list(validation.values()), split=len(rows))
model = LogisticRegression(C=50, class_weight="balanced", max_iter=2000, random_state=42)
model.fit(vectors[:len(rows)], list(rows.values()))
scores = model.decision_function(vectors[len(rows):])
order = np.argsort(scores, axis=1)
labels = model.classes_[order[:, -1]]
ranked = np.sort(scores, axis=1)
margins = ranked[:,-1] - ranked[:,-2]
# Choose the margin on separate development sentences. Break accuracy ties by fewer false awards.
best = None
for threshold in (0, .25, .5, .75, 1, 1.5, 2, 3):
    predicted = np.where(margins >= threshold, labels, "-")
    correct = int(np.sum(predicted == list(validation.values())))
    wrong_picks = int(np.sum((predicted != list(validation.values())) & (predicted != "-")))
    key = (correct, -wrong_picks, -threshold)
    if best is None or key > best[0]:
        best = (key, threshold)
data = {"format":1, "catalog":sorted(ids), "margin":best[1], "training_rows":len(rows),
        "validation_rows":len(validation), "validation_correct":best[0][0],
        "sklearn":sklearn.__version__, "heads":[{"id":str(label), "bias":float(bias),
        "weights":w.tolist()} for label,bias,w in zip(model.classes_,model.intercept_,model.coef_)]}
args.output.write_text(json.dumps(data, separators=(",",":"))+"\n")
print({k:v for k,v in data.items() if k not in ("heads","catalog")})
