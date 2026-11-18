import math
import sys

def checked_number(value):
    if not math.isfinite(value) or abs(value) > 1e15:
        raise ValueError("number outside the finite +/-1e15 domain")
    return value

def parse_number(text):
    text = text.strip()
    if not text or "_" in text or any(c in text for c in "xXpP"):
        raise ValueError("expected a decimal number")
    return checked_number(float(text))

def checked_line(raw, index):
    if index > 100000:
        raise ValueError("input exceeds 100000 rows")
    line = raw.removesuffix("\n").removesuffix("\r")
    if len(line) > 16384 or any((ord(c)<32 and c!="\t") or ord(c)>=127 for c in line):
        raise ValueError("expected printable ASCII and tabs, at most 16384 characters")
    return line

def format_number(value):
    return format(checked_number(value), ".17g")
