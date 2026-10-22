"""Shared scoring for unllm's question-answering measurements.

Every tool that marks unllm's answers right or wrong uses these, so all
reports count the same way:

  run_unllm(binary, question, layers=None)  -> {"branch", "answer", "passages"}
  outcome(branch, answer)                   -> "answered" | "hedged" | "abstained"
  nq_right(text, aliases)                   -> an accepted answer appears as whole words
  pattern_right(text, pattern)              -> a regex answer pattern matches
  exact_match(short, aliases)               -> SQuAD-style exact match, for short answers

Matching is by whole words after normalizing: lower case, no punctuation,
no a/an/the, dashes as spaces, and number words up to twenty plus
"hundred/thousand/million/billion" as digits. So "11" no longer matches
"110", and "four" matches "4" but not "fourth".
"""

import os
import re
import string
import subprocess

NO_ANSWER = re.compile(r"^(I couldn't find that|No reference packs are installed|That is a question about this machine)")
HEDGE = "The closest thing I know"

_WORDS = {w: str(i) for i, w in enumerate(
    "zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen "
    "seventeen eighteen nineteen twenty".split())}
_WORDS.update({"thirty": "30", "forty": "40", "fifty": "50", "sixty": "60", "seventy": "70", "eighty": "80",
               "ninety": "90", "hundred": "100", "thousand": "1000", "million": "1000000",
               "billion": "1000000000"})
_PUNCT = str.maketrans({c: " " for c in string.punctuation + "–—‘’“”"})


def tokens(s):
    s = s.lower().translate(_PUNCT)
    out = []
    for w in s.split():
        if w in ("a", "an", "the"):
            continue
        out.append(_WORDS.get(w, w))
    return out


def normalize(s):
    return " ".join(tokens(s))


def contains_words(text, phrase):
    t, p = tokens(text), tokens(phrase)
    if not p:
        return False
    n = len(p)
    return any(t[i:i + n] == p for i in range(len(t) - n + 1))


def nq_right(text, aliases):
    return any(contains_words(text, a) for a in aliases if tokens(a))


def pattern_right(text, pattern):
    return re.search(pattern, text, re.I) is not None


def exact_match(short, aliases):
    s = normalize(short)
    return bool(s) and any(s == normalize(a) for a in aliases)


def outcome(branch, answer):
    if not answer or branch == "none" or branch.startswith("category:") or NO_ANSWER.match(answer):
        return "abstained"
    if answer.startswith(HEDGE):
        return "hedged"
    return "answered"


def run_unllm(binary, question, layers=None, passages=20, timeout=180):
    env = dict(os.environ)
    if layers is not None:
        env["UNLLM_LAYERS"] = layers
    out = subprocess.run([binary, "--branch", "--brief", "--passages", str(passages), question],
                         capture_output=True, text=True, timeout=timeout, env=env).stdout
    answer, branch, brief, ps = [], "", [], []
    for line in out.splitlines():
        if line.startswith("[branch: "):
            branch = line[9:-1]
        elif line.startswith("[brief] "):
            brief.append(line[8:])
        elif line.startswith("[passage] "):
            ps.append(line[10:])
        elif not branch and not line.startswith("  "):
            answer.append(line)
    answer = " ".join(answer).strip()
    # A direct answer (fact box, opening line) has no ranked list: it is the list.
    if not ps and branch.startswith("world:"):
        ps = [answer] + brief
    return {"branch": branch, "answer": answer, "passages": ps}
