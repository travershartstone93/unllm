#!/usr/bin/env python3
"""One-time export of the meaning map unllm ships.

Usage: export_vectors.py MODEL_DIR OUT_BIN [LEXICON...] [--format=unl2]

MODEL_DIR holds a distilled static-embedding model (model2vec format).
Every word of the lexicon files, plus the built-in list below, is encoded
once; the result is a plain binary table the C++ loader reads. After this
script runs, no model is ever needed again.

The small table (default, "UNLV") ships with unllm. --format=unl2 writes the
large local table instead: add a WordNet json zip as a lexicon to cover the
whole language, and the model's own whole-word vocabulary is added too.
"""

import os
import struct
import sys

COMMON = """
slow fast hot cold warm cool loud quiet busy idle heavy light big small
high low full empty open closed broken fine stuck frozen dead alive
crash freeze hang stall lag stutter choke crawl drag race spin wait
burn boil roast bake cook melt glow smoke steam scorch swelter
run start stop quit exit launch open close kill end restart reboot
install remove update upgrade patch download upload sync fetch push
memory ram swap disk drive storage space file folder cache buffer
network internet wifi connection socket port address server computer
machine laptop desktop system device hardware software driver kernel
program application process service task job thread window screen
battery power energy charge drain plug cable adapter voltage
fan heat temperature degree thermal sensor cooling noise
monitor keyboard mouse camera speaker microphone printer
safe unsafe secure insecure suspicious trusted unknown strange odd
error warning problem issue fault failure bug glitch corrupt
yesterday today tomorrow morning afternoon evening night week month
minute hour second day time clock schedule
work break fix repair change modify configure setting option
read write copy move delete create save load search find
sluggish lethargic unresponsive snappy speedy laggy glacial
roasting boiling scorching sweltering freezing chilly toasty
noisy screaming roaring whining silent
bricked wedged borked
"""


def wordnet_lemmas(path):
    """Every single-word lemma in an Open English WordNet json zip."""
    import json
    import zipfile
    z = zipfile.ZipFile(path)
    out = set()
    for name in z.namelist():
        if os.path.basename(name).startswith("entries"):
            out.update(w for w in json.loads(z.read(name)) if w.isalpha())
    return out


def model_words(model_dir):
    """Whole-word entries of the model's own tokenizer vocabulary."""
    import json
    try:
        vocab = json.load(open(os.path.join(model_dir, "tokenizer.json")))["model"]["vocab"]
    except (OSError, KeyError, ValueError):
        return set()
    return {w for w in vocab if w.isalpha() and not w.startswith("##")}


def write_unl2(path, words, vectors):
    """The large table: unit rows quantized to 8 bits, one scale per row."""
    import numpy as np
    norms = np.linalg.norm(vectors, axis=1, keepdims=True)
    norms[norms == 0] = 1
    unit = vectors / norms
    scales = np.abs(unit).max(axis=1) / 127.0
    scales[scales == 0] = 1
    rows = np.clip(np.rint(unit / scales[:, None]), -127, 127).astype("<i1")
    blob = b""
    offsets = [0]
    parts = []
    for w in words:
        b = w.encode()
        parts.append(b)
        offsets.append(offsets[-1] + len(b))
    blob = b"".join(parts)
    with open(path, "wb") as f:
        f.write(b"UNL2")
        f.write(struct.pack("<II", len(words), vectors.shape[1]))
        f.write(np.asarray(offsets, dtype="<u4").tobytes())
        f.write(scales.astype("<f4").tobytes())
        f.write(rows.tobytes())
        f.write(blob)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) < 2:
        sys.exit(__doc__)
    from model2vec import StaticModel

    model = StaticModel.from_pretrained(args[0])
    words = set(COMMON.split())
    big = "--format=unl2" in flags
    for path in args[2:]:
        if path.endswith(".zip"):
            words.update(wordnet_lemmas(path))
            continue
        for line in open(path):
            line = line.split("#")[0].replace(":", " ")
            words.update(w for w in line.split() if w.isalpha())
    if big:
        words.update(model_words(args[0]))
    words = sorted({w.lower() for w in words if 2 < len(w) < 32}, key=lambda w: w.encode())
    vectors = model.encode(words)
    dim = vectors.shape[1]
    if big:
        write_unl2(args[1], words, vectors)
        print("wrote %d words, %d dimensions (UNL2)" % (len(words), dim))
        return
    with open(args[1], "wb") as f:
        f.write(b"UNLV")
        f.write(struct.pack("<II", len(words), dim))
        for w, v in zip(words, vectors):
            b = w.encode()
            f.write(struct.pack("B", len(b)))
            f.write(b)
            f.write(v.astype("<f4").tobytes())
    print("wrote %d words, %d dimensions" % (len(words), dim))


if __name__ == "__main__":
    main()
