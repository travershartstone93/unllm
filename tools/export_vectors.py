#!/usr/bin/env python3
"""One-time export of the meaning map unllm ships.

Usage: export_vectors.py MODEL_DIR OUT_BIN [LEXICON...]

MODEL_DIR holds a distilled static-embedding model (model2vec format).
Every word of the lexicon files, plus the built-in list below, is encoded
once; the result is a plain binary table the C++ loader reads. After this
script runs, no model is ever needed again.
"""

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


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    from model2vec import StaticModel

    model = StaticModel.from_pretrained(sys.argv[1])
    words = set(COMMON.split())
    for path in sys.argv[3:]:
        for line in open(path):
            line = line.split("#")[0].replace(":", " ")
            words.update(w for w in line.split() if w.isalpha())
    words = sorted(w.lower() for w in words if 2 < len(w) < 32)
    vectors = model.encode(words)
    dim = vectors.shape[1]
    with open(sys.argv[2], "wb") as f:
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
