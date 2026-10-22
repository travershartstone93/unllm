#!/usr/bin/env python3
"""unllm-ingest: turn reference sources into knowledge packs unllm can search.

A pack is a folder: manifest.json plus one or more shard-NNN.sqlite files,
each holding documents, passages and full-text indexes (SQLite FTS5). Every
shard also carries the manifest's keys in its meta table, so the C++ reader
needs no JSON. Packs live in ~/.local/share/unllm/packs and are built on this
machine from the sources' own files; no dataset text ships with unllm.

  unllm-ingest add PATH... [--name N] [--force] build packs from files or folders, in order
  unllm-ingest progress [--watch]               bars for the build running now and the ones queued
  unllm-ingest inbox [--night-only]             build whatever is in the inbox
  unllm-ingest list                             installed packs
  unllm-ingest check [NAME]                     measure text defects in installed packs
  unllm-ingest remove NAME                      delete a pack

Recognized inputs: FineWiki parquet (HuggingFaceFW/finewiki, Wikipedia
rendered from its own HTML, the recommended source) and the older
wikimedia/wikipedia parquet (a file or the folder of them), StackExchange
archive.org dumps (*.7z), Open English WordNet json zips, kaikki.org
Wiktionary jsonl, the Gutenberg poetry corpus (ndjson.gz with pg_catalog.csv
beside it), and folders of txt, md or pdf.

Every build is checked before it is installed: a sample of its passages is
measured for text defects (numbers lost from the source, leftover markup,
broken characters), the rates are recorded in the manifest, and a pack whose
text is badly damaged is not installed.

Long builds are gentle on the machine: niced, one shard at a time per
worker, a pause whenever the CPU passes 85 C, and resumable per input file.
"""

import argparse
import csv
import glob
import gzip
import html
import html.parser
import json
import os
import random
import re
import shutil
import sqlite3
import subprocess
import sys
import time
import zipfile

HOME = os.path.expanduser("~")
DATA = os.environ.get("UNLLM_DATA", os.path.join(os.environ.get("XDG_DATA_HOME", HOME + "/.local/share"), "unllm"))
PACKS = os.path.join(DATA, "packs")
INBOX = os.path.join(DATA, "inbox")
LOG = os.path.join(DATA, "ingest.log")
PROGRESS = os.path.join(DATA, "progress")
QUEUE = os.path.join(PROGRESS, "queue.json")
KNOWLEDGE = os.environ.get("UNLLM_KNOWLEDGE", os.path.join(HOME, "Downloads", "unllm-knowledge"))
INGEST_VERSION = "4"  # raised whenever extraction changes, so every pack rebuilds

csv.field_size_limit(1 << 30)


def log(msg):
    os.makedirs(DATA, exist_ok=True)
    line = time.strftime("%Y-%m-%d %H:%M:%S ") + msg
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\n")


# ---- gentle on a hot laptop ----

def cpu_temp():
    best = None
    for hw in glob.glob("/sys/class/hwmon/hwmon*"):
        try:
            name = open(os.path.join(hw, "name")).read().strip()
        except OSError:
            continue
        if name not in ("coretemp", "k10temp", "zenpower", "cpu_thermal"):
            continue
        for t in glob.glob(os.path.join(hw, "temp*_input")):
            try:
                v = int(open(t).read()) / 1000.0
                best = v if best is None else max(best, v)
            except (OSError, ValueError):
                pass
    return best


def cool_down(limit=85.0):
    t = cpu_temp()
    while t is not None and t > limit:
        log(f"cpu at {t:.0f} C, pausing 60 s")
        time.sleep(60)
        t = cpu_temp()


# ---- progress: small status files the progress command draws ----
# A shard build says how far it is through its source (articles of a
# Wikipedia file, bytes of a StackExchange posts file) in
# progress/<pack>.shard-NNN.json; a pack build counts its finished shards in
# progress/<pack>.json; an add of several sources lists them in
# progress/queue.json. Each file goes away when its work is done.

def save_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f"{path}.{os.getpid()}.tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f)
    os.replace(tmp, path)


def load_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def drop_file(path):
    try:
        os.remove(path)
    except OSError:
        pass


def alive(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, ValueError, TypeError):
        return False


def human_bytes(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1000:
            return f"{n:.0f} {unit}" if unit in ("B", "KB") or n >= 100 else f"{n:.1f} {unit}"
        n /= 1000.0
    return f"{n:.1f} TB"


def human_time(seconds):
    s = int(max(0, seconds))
    if s < 60:
        return f"{s} s"
    if s < 3600:
        return f"{s // 60} min"
    return f"{s // 3600} h {s % 3600 // 60} min"


class Ticker:
    """One shard build's progress. Reading the source is 85% of the work,
    building the search index 10%, compacting the file the last 5%."""

    SPANS = {"reading": (0.0, 0.85), "indexing": (0.85, 0.95), "compacting": (0.95, 1.0)}

    def __init__(self, pack, shard, shards):
        self.path = os.path.join(PROGRESS, f"{pack}.shard-{shard:03d}.json")
        self.info = {"pack": pack, "shard": shard, "shards": shards, "phase": "reading", "fraction": 0.0,
                     "detail": "starting", "started": time.time(), "pid": os.getpid()}
        self.last = 0.0
        self.save()

    def tick(self, part, detail="", phase="reading", force=False):
        """part: how far through this phase, from 0 to 1."""
        if not force and time.time() - self.last < 2.0:
            return
        lo, hi = self.SPANS.get(phase, self.SPANS["reading"])
        if phase != self.info.get("phase") or "phase_started" not in self.info:
            self.info["phase_started"] = time.time()
        self.info.update(phase=phase, detail=detail, fraction=round(lo + (hi - lo) * max(0.0, min(1.0, part)), 4))
        self.save()

    def save(self):
        self.info["updated"] = self.last = time.time()
        save_json(self.path, self.info)

    def done(self):
        drop_file(self.path)


# ---- text helpers ----

ARTICLES = ("the ", "a ", "an ")


def title_key(title):
    """lowercase, letters and digits only, leading article dropped (the C++ side matches this)."""
    t = re.sub(r"[^0-9a-z]+", " ", title.lower()).strip()
    for a in ARTICLES:
        if t.startswith(a):
            t = t[len(a):]
            break
    return t


SENT_END = re.compile(r"(?<=[.!?])\s+(?=[A-Z0-9\"'(])")


def split_long(text, target=150):
    """Split a paragraph longer than ~180 words at sentence ends near target words."""
    words = text.split()
    if len(words) <= 180:
        return [text]
    out, cur = [], []
    for s in SENT_END.split(text):
        cur.append(s)
        if sum(len(x.split()) for x in cur) >= target:
            out.append(" ".join(cur))
            cur = []
    if cur:
        if out and sum(len(x.split()) for x in cur) < 40:
            out[-1] += " " + " ".join(cur)
        else:
            out.append(" ".join(cur))
    return out


# Invisible characters some sources leave in text (byte order marks,
# zero-width spaces and joiners, direction marks).
INVISIBLE = dict.fromkeys(map(ord, "﻿​‌‍‎‏⁠­"), None)


def clean_inline(text):
    """Plain text from a line that may carry light markdown or odd spacing."""
    t = text.translate(INVISIBLE).replace("\xa0", " ")
    t = re.sub(r"\*\*([^*\n]+)\*\*", r"\1", t)
    t = re.sub(r"\[([^\]\n]+)\]\([^)\n]*\)", r"\1", t)
    t = re.sub(r"`([^`\n]+)`", r"\1", t)
    t = re.sub(r"[ \t]+", " ", t)
    t = re.sub(r" ([,.;:!?)])", r"\1", t)
    t = re.sub(r"\(\s*[,;]?\s*\)", "", t)
    return t.strip()


# ---- maths: LaTeX to readable text ----
# Wikipedia keeps formulas as LaTeX ({\displaystyle E=mc^{2}}) and the maths
# StackExchange sites write them between dollar signs. Answers read better
# as E=mc², (1/2)mv², CO₂, so formulas are rewritten with Unicode where a
# symbol exists and with plain characters where it does not.

GREEK = {
    "alpha": "α", "beta": "β", "gamma": "γ", "delta": "δ", "epsilon": "ε", "varepsilon": "ε", "zeta": "ζ",
    "eta": "η", "theta": "θ", "vartheta": "ϑ", "iota": "ι", "kappa": "κ", "lambda": "λ", "mu": "μ", "nu": "ν",
    "xi": "ξ", "pi": "π", "varpi": "ϖ", "rho": "ρ", "varrho": "ϱ", "sigma": "σ", "varsigma": "ς", "tau": "τ",
    "upsilon": "υ", "phi": "φ", "varphi": "φ", "chi": "χ", "psi": "ψ", "omega": "ω", "Gamma": "Γ",
    "Delta": "Δ", "Theta": "Θ", "Lambda": "Λ", "Xi": "Ξ", "Pi": "Π", "Sigma": "Σ", "Upsilon": "Υ",
    "Phi": "Φ", "Psi": "Ψ", "Omega": "Ω",
}
SYMBOLS = {
    "cdot": "·", "cdotp": "·", "times": "×", "div": "÷", "pm": "±", "mp": "∓", "leq": "≤", "le": "≤",
    "geq": "≥", "ge": "≥", "neq": "≠", "ne": "≠", "approx": "≈", "sim": "~", "simeq": "≃", "cong": "≅",
    "equiv": "≡", "propto": "∝", "infty": "∞", "partial": "∂", "nabla": "∇", "to": "→", "rightarrow": "→",
    "Rightarrow": "⇒", "leftarrow": "←", "Leftarrow": "⇐", "leftrightarrow": "↔", "Leftrightarrow": "⇔",
    "mapsto": "↦", "implies": "⇒", "iff": "⇔", "in": "∈", "notin": "∉", "ni": "∋", "subset": "⊂",
    "subseteq": "⊆", "supset": "⊃", "supseteq": "⊇", "cup": "∪", "cap": "∩", "setminus": "∖",
    "emptyset": "∅", "varnothing": "∅", "forall": "∀", "exists": "∃", "neg": "¬", "lnot": "¬", "land": "∧",
    "wedge": "∧", "lor": "∨", "vee": "∨", "oplus": "⊕", "otimes": "⊗", "sum": "Σ", "prod": "Π", "int": "∫",
    "iint": "∬", "oint": "∮", "circ": "∘", "bullet": "•", "ldots": "…", "cdots": "⋯", "dots": "…",
    "vdots": "⋮", "hbar": "ħ", "ell": "ℓ", "angle": "∠", "perp": "⊥", "parallel": "∥", "prime": "′",
    "langle": "⟨", "rangle": "⟩", "lceil": "⌈", "rceil": "⌉", "lfloor": "⌊", "rfloor": "⌋", "mid": "|",
    "vert": "|", "Vert": "‖", "star": "⋆", "ast": "*", "degree": "°", "deg": "°", "aleph": "ℵ",
    "Re": "ℜ", "Im": "ℑ", "uparrow": "↑", "downarrow": "↓", "rightleftharpoons": "⇌", "ll": "≪", "gg": "≫",
    "lt": "<", "gt": ">", "colon": ":", "triangle": "△", "square": "□", "checkmark": "✓",
}
FUNCTIONS = {"sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
             "log", "ln", "lg", "exp", "lim", "liminf", "limsup", "max", "min", "sup", "inf", "det", "dim",
             "ker", "gcd", "lcm", "arg", "mod", "bmod", "pmod", "Pr", "hom", "rank", "tr", "sgn"}
WORDLIKE = {"text", "textrm", "textbf", "textit", "textsf", "texttt", "mbox", "emph", "mathrm", "mathbf",
            "mathit", "mathsf", "mathtt", "mathcal", "mathscr", "mathfrak", "boldsymbol", "bm", "operatorname",
            "vec", "hat", "widehat", "bar", "overline", "underline", "tilde", "widetilde", "dot", "ddot",
            "overrightarrow", "mathring", "check", "breve", "acute", "grave", "displaystyle", "unicode"}
DROPPED = {"left", "right", "big", "Big", "bigg", "Bigg", "bigl", "bigr", "Bigl", "Bigr", "biggl", "biggr",
           "middle", "displaystyle", "textstyle", "scriptstyle", "scriptscriptstyle", "limits", "nolimits",
           "nonumber", "notag", "quad", "qquad", "hfill", "centering", "small", "large", "Large", "normalsize",
           "phantom", "hphantom", "vphantom", "label", "color", "boxed", "require", "newcommand"}
BLACKBOARD = {"R": "ℝ", "N": "ℕ", "Z": "ℤ", "Q": "ℚ", "C": "ℂ", "P": "ℙ", "E": "𝔼", "F": "𝔽", "H": "ℍ"}
SUP = dict(zip("0123456789+-=()niaxyt", "⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ⁿⁱᵃˣʸᵗ"))
SUB = dict(zip("0123456789+-=()aeoxnijkmpstrhlu", "₀₁₂₃₄₅₆₇₈₉₊₋₌₍₎ₐₑₒₓₙᵢⱼₖₘₚₛₜᵣₕₗᵤ"))
_TEX_CMD = re.compile(r"\\([A-Za-z]+|.)", re.S)


def _tex_arg(s, i):
    """The argument starting at s[i]: a {group}, a \\command or one character. (text, next index)."""
    while i < len(s) and s[i] == " ":
        i += 1
    if i >= len(s):
        return "", i
    if s[i] == "{":
        depth, k = 0, i
        while k < len(s):
            if s[k] == "{":
                depth += 1
            elif s[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        return s[i + 1:k], k + 1
    if s[i] == "\\":
        m = _TEX_CMD.match(s, i)
        return (s[i:m.end()], m.end()) if m else ("", i + 1)
    return s[i], i + 1


def _tex_wrap(x):
    return x if re.fullmatch(r"[\w.·′]+", x) else f"({x})"


def _tex_script(x, mark):
    table = SUP if mark == "^" else SUB
    if x and all(ch in table for ch in x):
        return "".join(table[ch] for ch in x)
    return mark + (x if len(x) == 1 else f"({x})")


def latex_to_text(tex):
    """Readable text for a LaTeX formula: E=mc^{2} -> E=mc², \\frac{1}{2} -> (1/2)."""
    out, i, s = [], 0, tex
    while i < len(s):
        c = s[i]
        if c == "\\":
            m = _TEX_CMD.match(s, i)
            if not m:  # a backslash ending the formula
                break
            name, i = m.group(1), m.end()
            if name in ("frac", "dfrac", "tfrac", "cfrac"):
                a, i = _tex_arg(s, i)
                b, i = _tex_arg(s, i)
                out.append(f"({_tex_wrap(latex_to_text(a))}/{_tex_wrap(latex_to_text(b))})")
            elif name == "binom":
                a, i = _tex_arg(s, i)
                b, i = _tex_arg(s, i)
                out.append(f"C({latex_to_text(a)}, {latex_to_text(b)})")
            elif name == "sqrt":
                root = ""
                if i < len(s) and s[i] == "[":
                    k = s.find("]", i)
                    if k > 0:
                        root, i = s[i + 1:k], k + 1
                a, i = _tex_arg(s, i)
                sign = {"": "√", "2": "√", "3": "∛", "4": "∜"}.get(root.strip(), root.strip() + "√")
                out.append(sign + _tex_wrap(latex_to_text(a)))
            elif name == "ce":
                a, i = _tex_arg(s, i)
                a = a.replace("<=>", "⇌").replace("->", "→").replace("<-", "←")
                a = re.sub(r"(?<=[A-Za-z)\]])(\d+)", lambda g: "".join(SUB[d] for d in g.group(1)), a)
                out.append(latex_to_text(a))
            elif name == "mathbb":
                a, i = _tex_arg(s, i)
                out.append(BLACKBOARD.get(a.strip(), a))
            elif name in ("text", "textrm", "mbox", "textit", "textbf", "emph"):
                a, i = _tex_arg(s, i)
                out.append(a)
            elif name in WORDLIKE:
                a, i = _tex_arg(s, i)
                out.append(latex_to_text(a))
            elif name in ("begin", "end"):
                env, i = _tex_arg(s, i)
                if name == "begin" and env.strip() in ("array", "tabular") and i < len(s) and s[i] == "{":
                    _, i = _tex_arg(s, i)
                out.append(" ")
            elif name == "tag":
                a, i = _tex_arg(s, i)
                out.append(f" ({a})")
            elif name in DROPPED:
                if name in ("label", "color", "phantom", "hphantom", "vphantom", "require"):
                    _, i = _tex_arg(s, i)
                if name in ("left", "right", "middle") and i < len(s) and s[i] == ".":
                    i += 1
                out.append(" " if name in ("quad", "qquad") else "")
            elif name in GREEK:
                out.append(GREEK[name])
            elif name in SYMBOLS:
                out.append(SYMBOLS[name])
            elif name in FUNCTIONS:
                out.append(name + " ")
            elif name == "\\":
                out.append("; ")
            elif name in (",", ";", ":", "!", " ", ">"):
                out.append(" " if name != "!" else "")
            elif len(name) == 1 and not name.isalpha():
                out.append(name)  # \{ \} \$ \% \& \_ \#
            else:
                out.append(name)
        elif c in "^_":
            a, i = _tex_arg(s, i + 1)
            out.append(_tex_script(latex_to_text(a), c))
        elif c in "{}":
            i += 1
        elif c in "&~":
            out.append(" ")
            i += 1
        else:
            out.append(c)
            i += 1
    t = re.sub(r"\s+", " ", "".join(out)).strip()
    return re.sub(r"\(\s+", "(", re.sub(r"\s+([,.;)])", r"\1", t))


def detex_display(text):
    """Every {\\displaystyle ...} formula in Wikipedia text rewritten as readable text."""
    out, i = [], 0
    while True:
        j = text.find("{\\displaystyle", i)
        if j < 0:
            out.append(text[i:])
            return "".join(out)
        out.append(text[i:j])
        depth, k = 0, j
        while k < len(text):
            if text[k] == "{":
                depth += 1
            elif text[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        out.append(latex_to_text(text[j + len("{\\displaystyle"):k]))
        i = k + 1


DOLLAR_MATH = re.compile(r"\$\$(.+?)\$\$|\$([^$\n]+?)\$|\\\[(.+?)\\\]|\\\((.+?)\\\)", re.S)


def detex_dollars(text):
    """MathJax formulas ($...$, $$...$$, \\(...\\), \\[...\\]) rewritten as readable text.
    Only for sites where a dollar sign starts maths, never shell or prices."""
    text = text.replace("\\$", "\x00")
    text = DOLLAR_MATH.sub(lambda m: latex_to_text(next(g for g in m.groups() if g is not None)), text)
    return text.replace("\x00", "$")


# ---- HTML (StackExchange posts) ----

class TextOf(html.parser.HTMLParser):
    """HTML to blocks: ("text", paragraph) for <p>, headings and the like,
    ("item", text) for a list item, and ("code", block) for <pre>, with the
    block's own line breaks kept."""

    BREAK = {"p", "li", "br", "h1", "h2", "h3", "h4", "h5", "h6", "pre", "blockquote", "tr", "div", "hr"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.blocks, self.cur, self.pre, self.li = [], [], 0, 0

    def handle_starttag(self, tag, attrs):
        if tag in self.BREAK and not self.pre:
            self.flush()
        if tag == "pre":
            self.pre += 1
        elif tag == "li":
            self.li += 1

    def handle_endtag(self, tag):
        if tag == "pre" and self.pre:
            self.flush()
            self.pre -= 1
        elif tag in self.BREAK and not self.pre:
            self.flush()
            if tag == "li" and self.li:
                self.li -= 1

    def handle_data(self, data):
        self.cur.append(data)

    def flush(self):
        raw, self.cur = "".join(self.cur), []
        if self.pre:
            code = "\n".join(line.rstrip() for line in raw.translate(INVISIBLE).split("\n")).strip("\n")
            if code.strip():
                self.blocks.append(("code", code))
        else:
            t = re.sub(r"\s+", " ", raw.translate(INVISIBLE)).strip()
            if t:
                self.blocks.append(("item" if self.li else "text", t))

    def result(self):
        self.flush()
        return self.blocks


def html_blocks(body):
    p = TextOf()
    try:
        p.feed(body)
        p.close()
    except Exception:
        return [("text", re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", " ", body))).strip())]
    return p.result()


def html_paragraphs(body):
    return [t for _, t in html_blocks(body)]


# Pleasantries and edit markers that carry no answer on their own.
FILLER = re.compile(r"^(?:edit(?:ed)?(?: \d+)?|update(?:d)?(?: \d+)?|hope (?:this|that|it) helps|good luck|cheers|"
                    r"thanks|thank you|hth|regards|enjoy|hope this answers your question)[.!:]*$", re.I)


def tidy_blocks(blocks, maths=False):
    """Answer blocks ready to store, each readable on its own:
    - pleasantries and edit markers dropped;
    - a heading or label ("Ubuntu 18.04+", "Source:") starts the block after it;
    - a sentence ending in a colon ("Open a terminal and type this:") stays
      with the command or list it introduces;
    - a run of short list items becomes one list ("You need: 2 Tbsp oregano;
      1 tsp salt."), while longer items (steps) stay one block each;
    - a tiny sentence ("Works well.") joins the text before it;
    - formulas are rewritten as text on maths sites."""
    out, carry = [], ""

    def intro():
        return bool(out) and out[-1][0] == "text" and out[-1][1].endswith(":") and len(out[-1][1]) <= 300

    for kind, text in blocks:
        if kind in ("text", "item"):
            if maths:
                text = detex_dollars(text)
            text = clean_inline(text)
            if not text or FILLER.match(text):
                continue
            if kind == "item" and len(text) < 40:
                text = text.rstrip(".;, ")
                if out and out[-1][0] == "items" and not carry:
                    out[-1] = ("items", out[-1][1] + "; " + text)
                elif carry:
                    out.append(("items", carry + " " + text))
                    carry = ""
                elif intro():
                    out[-1] = ("items", out[-1][1] + " " + text)
                else:
                    out.append(("items", text))
                continue
            kind = "text"
            if len(text) < 25:
                if not text.endswith((".", "!", "?", ":")):
                    text += ":"  # a heading: it names what follows
                if text.endswith(":") or not out or out[-1][0] == "code":
                    carry = (carry + " " + text).strip()
                else:
                    out[-1] = ("text", out[-1][1] + " " + text)
                continue
        if kind == "code" and not carry and intro():
            out[-1] = ("code", out[-1][1] + "\n" + text)
            continue
        if carry:
            text = carry + ("\n" if kind == "code" else " ") + text
            carry = ""
        out.append((kind, text))
    if carry and not carry.endswith(":"):
        if out:
            out[-1] = (out[-1][0], out[-1][1] + " " + carry)
        else:
            out.append(("text", carry))
    return [("text", t if t.endswith((".", "!", "?", ":")) else t + ".") if k == "items" else (k, t) for k, t in out]


# ---- shards ----

SCHEMA = """
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE docs(doc_id INTEGER PRIMARY KEY, title TEXT NOT NULL, title_key TEXT NOT NULL,
                  url TEXT, author TEXT, license TEXT, tags TEXT, score INTEGER, extra TEXT);
CREATE TABLE passages(pid INTEGER PRIMARY KEY, doc_id INTEGER NOT NULL, ord INTEGER NOT NULL,
                      section TEXT, role TEXT NOT NULL, text TEXT NOT NULL);
CREATE TABLE infobox(doc_id INTEGER NOT NULL, box TEXT, key TEXT NOT NULL, value TEXT NOT NULL);
"""
FTS = """
CREATE INDEX docs_title_key ON docs(title_key);
CREATE INDEX passages_doc ON passages(doc_id, ord);
CREATE INDEX infobox_doc ON infobox(doc_id);
CREATE VIRTUAL TABLE passages_fts USING fts5(text, content='passages', content_rowid='pid',
       tokenize='porter unicode61 remove_diacritics 2', detail='{detail}');
CREATE VIRTUAL TABLE titles_fts USING fts5(title, content='docs', content_rowid='doc_id',
       tokenize='porter unicode61 remove_diacritics 2');
INSERT INTO passages_fts(passages_fts) VALUES('rebuild');
INSERT INTO titles_fts(titles_fts) VALUES('rebuild');
INSERT INTO passages_fts(passages_fts) VALUES('optimize');
CREATE VIRTUAL TABLE fts_vocab USING fts5vocab(passages_fts, 'row');
CREATE TABLE vocab(term TEXT PRIMARY KEY, df INTEGER) WITHOUT ROWID;
INSERT INTO vocab SELECT term, doc FROM fts_vocab;
DROP TABLE fts_vocab;
"""


class Shard:
    def __init__(self, path, meta, detail="full"):
        self.path = path
        self.detail = detail
        if os.path.exists(path):
            os.remove(path)
        self.db = sqlite3.connect(path)
        self.db.executescript("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA cache_size=-65536;"
                              "PRAGMA application_id=1431194704; PRAGMA user_version=1;")
        self.db.executescript(SCHEMA)
        self.db.executemany("INSERT INTO meta VALUES(?,?)", [(k, str(v)) for k, v in meta.items()])
        self.doc_id = 0
        self.pid = 0
        self.pending = 0
        self.docs = 0
        self.passages = 0
        self.ticker = None

    def tick(self, part, detail="", force=False):
        """How far the reader is through its source, from 0 to 1."""
        if self.ticker:
            self.ticker.tick(part, detail, force=force)

    def add_doc(self, title, url="", author="", license="", tags="", score=0, extra=""):
        self.doc_id += 1
        self.docs += 1
        self.db.execute("INSERT INTO docs VALUES(?,?,?,?,?,?,?,?,?)",
                        (self.doc_id, title, title_key(title), url, author, license, tags, score, extra))
        return self.doc_id

    def add_passage(self, doc_id, ord_, role, text, section=""):
        text = text.strip()
        if not text:
            return
        self.pid += 1
        self.passages += 1
        self.db.execute("INSERT INTO passages VALUES(?,?,?,?,?,?)", (self.pid, doc_id, ord_, section, role, text))
        self.pending += 1
        if self.pending >= 20000:
            self.db.commit()
            self.pending = 0

    def add_fact(self, doc_id, box, key, value):
        """One row of an article's fact box ("Elevation", "8,848.86 m")."""
        key, value = clean_inline(key), clean_inline(value)
        if key and value:
            self.db.execute("INSERT INTO infobox VALUES(?,?,?,?)", (doc_id, box, key, value))

    def finish(self):
        self.db.commit()
        if self.ticker:
            self.ticker.tick(0, "building the search index", phase="indexing", force=True)
        self.db.executescript(FTS.replace("{detail}", self.detail))
        self.db.execute("INSERT OR REPLACE INTO meta VALUES('docs', ?)", (str(self.docs),))
        self.db.execute("INSERT OR REPLACE INTO meta VALUES('passages', ?)", (str(self.passages),))
        self.db.commit()
        if self.ticker:
            self.ticker.tick(0, "compacting the file", phase="compacting", force=True)
        self.db.execute("VACUUM")
        self.db.close()


# ---- readers: each yields shards' worth of documents through a callback ----

DROP_SECTIONS = {"references", "external links", "see also", "notes", "further reading", "bibliography",
                 "sources", "related pages", "other websites", "footnotes", "citations", "works cited",
                 "notes and references", "references and notes", "explanatory notes", "external sources",
                 "sources and further reading", "references and further reading", "further reading and external links",
                 "notes and sources", "general references", "cited sources", "selected bibliography", "literature",
                 "gallery", "other pages", "links", "web links", "online sources", "primary sources",
                 "secondary sources", "citations and notes", "endnotes"}


def wiki_passages(text):
    """(section, role, text) passages from one plain-text Wikipedia article."""
    out, section, buf, words = [], "", [], 0
    first = True

    def emit():
        nonlocal buf, words, first
        if buf:
            for piece in split_long(" ".join(buf)):
                out.append((section, "lead" if first else "body", piece))
                first = False
        buf, words = [], 0

    for block in text.split("\n\n"):
        b = block.strip()
        if not b:
            continue
        one_line = "\n" not in b
        if one_line and len(b) < 80 and not b.rstrip().endswith((".", "!", "?", ":", ";")):
            emit()
            section = b.strip()
            continue
        # A heading glued to its list ("Related pages\nPhotosynthesis\n...").
        head = b.split("\n", 1)[0].strip()
        if not one_line and len(head) < 80 and head.lower() in DROP_SECTIONS:
            emit()
            section = head
            continue
        if section.lower() in DROP_SECTIONS:
            continue
        b = re.sub(r"\s*\n\s*", " ", b)
        buf.append(b)
        words += len(b.split())
        if words >= 60:
            emit()
    emit()
    return out


def read_wikipedia_file(path, shard):
    import pyarrow.parquet as pq
    pf = pq.ParquetFile(path)
    total, seen, n = pf.metadata.num_rows or 1, 0, 0
    for batch in pf.iter_batches(batch_size=2000, columns=["id", "url", "title", "text"]):
        seen += batch.num_rows
        shard.tick(seen / total, f"{seen:,} of {total:,} articles")
        for row in batch.to_pylist():
            title, text = row.get("title") or "", row.get("text") or ""
            if not title or not text:
                continue
            ps = wiki_passages(text)
            if not ps:
                continue
            d = shard.add_doc(title, url=row.get("url") or "", license="CC BY-SA 4.0")
            for i, (sec, role, t) in enumerate(ps):
                shard.add_passage(d, i, role, t, sec)
            n += 1
            if n % 20000 == 0:
                cool_down()
    return n


LIST_ITEM = re.compile(r"^\s*(?:[-*+]|\d+[.)])\s+(.*)$")
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*$")
TABLE_RULE = re.compile(r":?-{2,}:?")


BARE_VERBS = {"is", "are", "was", "were", "has", "have", "had", "be", "been"}


def bare_label(line):
    """A short line with no sentence in it ("Boardroom", "VfB Stuttgart", "Studio albums")."""
    w = line.split()
    return len(w) <= 4 and not line.endswith((".", "!", "?", ";", ",")) and not {x.lower() for x in w} & BARE_VERBS


def finewiki_passages(text):
    """(section, role, text) passages from one FineWiki article (markdown).

    Prose is packed into passages of 60 to 180 words, the first one being the
    lead. A run of short list items becomes one "list" passage headed by what
    introduced it: the line before it ("The main types are: red; blue."), a
    bare label line ("Boardroom: Ann Smith; Bob Jones."), or else its section
    name. Nested items stay with their parent ("July 2: Ann Smith, Bob
    Jones"); longer items read as prose. Tables become "table" passages, one
    row per line, and code blocks "code" passages with their line breaks.
    Link and reference sections are dropped, a bare label that labels nothing
    is dropped, and formulas are rewritten as text."""
    out = []
    section, drop_level = "", 0
    label, label_colon = "", False
    prose, words, items, rows, code = [], 0, [], [], None
    first = [True]

    def emit_prose():
        nonlocal prose, words, label
        if prose:
            for piece in split_long(" ".join(prose)):
                out.append((section, "lead" if first[0] else "body", piece))
                first[0] = False
            label = ""
        prose, words = [], 0

    def emit_items():
        nonlocal items, label
        if not items:
            return
        name = label or section
        head = name + ": " if name else ""
        label = ""
        elems, group = [], None  # group: [title, indent, members, nested]

        def close():
            nonlocal group
            if group:
                elems.append(group[0] + (": " + ", ".join(group[2]) if group[2] else ""))
            group = None

        for indent, it in items:
            if it.endswith(":") and len(it) > 1:
                close()
                group = [it[:-1].strip(), indent, [], None]
                continue
            if group is not None:
                if group[3] is None:
                    group[3] = indent > group[1]
                if (group[3] and indent > group[1]) or (not group[3]):
                    group[2].append(it.rstrip(".;, "))
                    continue
                close()
            elems.append(it.rstrip(".;, "))
        close()
        chunk = []
        for e in elems:
            chunk.append(e)
            if sum(len(x.split()) for x in chunk) >= 150:
                out.append((section, "list", head + "; ".join(chunk) + "."))
                chunk = []
        if chunk:
            out.append((section, "list", head + "; ".join(chunk) + "."))
        items = []

    def emit_rows():
        nonlocal rows, label
        if not rows:
            return
        top = label + ":\n" if label else ""
        label = ""
        chunk, n = [], 0
        for r in rows:
            chunk.append(r)
            n += len(r.split())
            if n >= 180:
                out.append((section, "table", top + "\n".join(chunk)))
                chunk, n = [], 0
        if chunk:
            out.append((section, "table", top + "\n".join(chunk)))
        rows = []

    def emit_all():
        emit_prose()
        emit_items()
        emit_rows()

    def add_prose(para):
        nonlocal words, label
        if label and label_colon:
            para = label + ": " + para
        label = ""
        prose.append(para)
        words += len(para.split())
        if words >= 60:
            emit_prose()

    for raw in text.split("\n"):
        line = raw.rstrip()
        stripped = line.strip()
        if code is not None:
            if stripped.startswith("```"):
                block = "\n".join(code).strip("\n")
                if block.strip() and not drop_level:
                    out.append((section, "code", block))
                code = None
            else:
                code.append(line.translate(INVISIBLE))
            continue
        if stripped.startswith("```"):
            emit_all()
            code = []
            continue
        h = HEADING.match(stripped)
        if h:
            emit_all()
            label = ""
            level = len(h.group(1))
            if level == 1:
                continue  # the article's title
            if drop_level and level > drop_level:
                continue  # a subsection of a dropped section
            name = clean_inline(h.group(2))
            drop_level = level if name.lower() in DROP_SECTIONS else 0
            section = name
            continue
        if drop_level:
            continue
        if not stripped:
            emit_items()
            emit_rows()
            continue
        if stripped.startswith("|") and stripped.endswith("|") and len(stripped) > 1:
            emit_prose()
            emit_items()
            cells = [clean_inline(detex_display(c)) for c in stripped.strip("|").split("|")]
            cells = [c for c in cells if c and not TABLE_RULE.fullmatch(c)]
            if cells:
                rows.append(", ".join(cells))
            continue
        m = LIST_ITEM.match(line)
        if m:
            emit_rows()
            item = clean_inline(detex_display(m.group(1)))
            if not item:
                continue
            if len(item.split()) >= 12 and not item.endswith(":"):
                emit_items()
                add_prose(item if item.endswith((".", "!", "?")) else item + ".")
            else:
                emit_prose()
                items.append((len(line) - len(line.lstrip()), item))
            continue
        emit_items()
        emit_rows()
        para = clean_inline(detex_display(stripped))
        if not para or not re.search(r"[^\W\d_]", para):
            continue  # no letters at all: a stray number ("8.", "12.")
        if para.endswith(":") and len(para.split()) <= 30:
            emit_prose()
            label, label_colon = para[:-1].rstrip(), True
            continue
        if bare_label(para):
            emit_prose()
            label, label_colon = para, False
            continue
        add_prose(para)
    if code and not drop_level:
        out.append((section, "code", "\n".join(code).strip("\n")))
    emit_all()
    return out


def read_finewiki_file(path, shard):
    import pyarrow.parquet as pq
    pf = pq.ParquetFile(path)
    have = set(pf.schema_arrow.names)
    cols = [c for c in ("title", "url", "text", "infoboxes", "date_modified", "wikidata_id") if c in have]
    total, seen, n = pf.metadata.num_rows or 1, 0, 0
    for batch in pf.iter_batches(batch_size=500, columns=cols):
        seen += batch.num_rows
        shard.tick(seen / total, f"{seen:,} of {total:,} articles")
        data = batch.to_pydict()
        for k in range(batch.num_rows):
            title, text = data["title"][k] or "", data["text"][k] or ""
            if not title:
                continue
            ps = finewiki_passages(text)
            facts = []
            boxes = data["infoboxes"][k] if "infoboxes" in data else None
            if boxes:
                try:
                    for box in json.loads(boxes):
                        for key, value in (box.get("data") or {}).items():
                            if not isinstance(value, str):
                                value = json.dumps(value, ensure_ascii=False)
                            facts.append((box.get("title") or "", key, value))
                except (ValueError, AttributeError, TypeError):
                    pass
            if not ps and not facts:
                continue
            extra = json.dumps({"modified": (data.get("date_modified") or [""] * batch.num_rows)[k] or "",
                                "wikidata": (data.get("wikidata_id") or [""] * batch.num_rows)[k] or ""})
            d = shard.add_doc(title, url=(data.get("url") or [""] * batch.num_rows)[k] or "",
                              license="CC BY-SA 4.0", extra=extra)
            for i, (sec, role, t) in enumerate(ps):
                shard.add_passage(d, i, role, t, sec)
            for box, key, value in facts:
                shard.add_fact(d, box, key, value)
            n += 1
            if n % 20000 == 0:
                cool_down()
    return n


def se_license(attrs):
    lic = attrs.get("ContentLicense") or ""
    if lic:
        return lic
    date = attrs.get("CreationDate", "")
    return "CC BY-SA 2.5" if date < "2011-04-08" else "CC BY-SA 3.0" if date < "2018-05-02" else "CC BY-SA 4.0"


ROW = re.compile(r'(\w+)="([^"]*)"')


def member_size(archive, member):
    """Uncompressed size of one file inside a 7z archive, or 0 when unknown."""
    try:
        out = subprocess.run(["7z", "l", "-slt", archive, member], capture_output=True, text=True, timeout=120).stdout
    except (OSError, subprocess.SubprocessError):
        return 0
    m = re.search(r"^Path = " + re.escape(member) + r"$.*?^Size = (\d+)", out, re.M | re.S)
    return int(m.group(1)) if m else 0


def xml_rows(archive, member, seen=None):
    """Stream <row .../> attributes from one file inside a 7z archive.
    seen: a one-item list that counts the bytes read, for progress."""
    p = subprocess.Popen(["7z", "e", "-so", archive, member], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        for raw in p.stdout:
            if seen is not None:
                seen[0] += len(raw)
            line = raw.decode("utf-8", "replace").lstrip("﻿").strip()
            if not line.startswith("<row "):
                continue
            yield {k: html.unescape(v) for k, v in ROW.findall(line)}
    finally:
        p.stdout.close()
        p.wait()


# Sites where a dollar sign starts a formula (MathJax). Everywhere else it is
# a shell variable or a price and is left alone.
MATH_SITES = {"math.stackexchange.com", "physics.stackexchange.com", "chemistry.stackexchange.com",
              "astronomy.stackexchange.com", "electronics.stackexchange.com", "biology.stackexchange.com",
              "stats.stackexchange.com", "mathoverflow.net", "cs.stackexchange.com", "quant.stackexchange.com",
              "engineering.stackexchange.com", "earthscience.stackexchange.com", "crypto.stackexchange.com",
              "economics.stackexchange.com", "cstheory.stackexchange.com", "matheducators.stackexchange.com",
              "hsm.stackexchange.com", "quantumcomputing.stackexchange.com"}


def read_stackexchange(archive, shard, site):
    tmp = shard.db
    tmp.executescript("CREATE TEMP TABLE q(id INTEGER PRIMARY KEY, title TEXT, tags TEXT, score INT, accepted INT,"
                      " body TEXT, owner INT, oname TEXT, lic TEXT);"
                      "CREATE TEMP TABLE a(id INTEGER PRIMARY KEY, parent INT, score INT, body TEXT, owner INT,"
                      " oname TEXT, lic TEXT); CREATE TEMP TABLE users(id INTEGER PRIMARY KEY, name TEXT);")
    n = 0
    posts_size, seen = member_size(archive, "Posts.xml"), [0]
    shard.tick(0, f"posts: 0 B of {human_bytes(posts_size)}", force=True)
    for r in xml_rows(archive, "Posts.xml", seen):
        t = r.get("PostTypeId")
        try:
            score = int(r.get("Score", "0"))
        except ValueError:
            score = 0
        if t == "1" and score >= 1:
            tmp.execute("INSERT INTO q VALUES(?,?,?,?,?,?,?,?,?)",
                        (int(r["Id"]), r.get("Title", ""), r.get("Tags", ""), score,
                         int(r.get("AcceptedAnswerId", "0") or 0), r.get("Body", ""), int(r.get("OwnerUserId", "0") or 0),
                         r.get("OwnerDisplayName", ""), se_license(r)))
        elif t == "2" and score >= 1:
            tmp.execute("INSERT INTO a VALUES(?,?,?,?,?,?,?)",
                        (int(r["Id"]), int(r.get("ParentId", "0") or 0), score, r.get("Body", ""),
                         int(r.get("OwnerUserId", "0") or 0), r.get("OwnerDisplayName", ""), se_license(r)))
        n += 1
        if n % 200000 == 0:
            tmp.commit()
            cool_down()
        if n % 20000 == 0 and posts_size:
            shard.tick(0.6 * seen[0] / posts_size, f"posts: {human_bytes(seen[0])} of {human_bytes(posts_size)}")
    tmp.commit()
    shard.tick(0.6, "matching questions with their best answers", force=True)
    # Keep, per question, the accepted answer, or else the best answer scoring 2 or more.
    tmp.executescript("CREATE INDEX temp.a_parent ON a(parent, score);"
                      "CREATE TEMP TABLE keep AS SELECT q.id AS qid, COALESCE("
                      " (SELECT a.id FROM a WHERE a.id = q.accepted AND a.parent = q.id),"
                      " (SELECT a.id FROM a WHERE a.parent = q.id AND a.score >= 2 ORDER BY a.score DESC LIMIT 1)) AS aid"
                      " FROM q; DELETE FROM keep WHERE aid IS NULL; CREATE INDEX keep_aid ON keep(aid);")
    needed = set()
    for (owner,) in tmp.execute("SELECT owner FROM q JOIN keep ON keep.qid = q.id UNION "
                                "SELECT owner FROM a JOIN keep ON keep.aid = a.id"):
        if owner:
            needed.add(owner)
    users_size, useen, k = member_size(archive, "Users.xml"), [0], 0
    try:
        for r in xml_rows(archive, "Users.xml", useen):
            k += 1
            if k % 20000 == 0 and users_size:
                shard.tick(0.6 + 0.1 * useen[0] / users_size,
                           f"authors: {human_bytes(useen[0])} of {human_bytes(users_size)}")
            uid = int(r.get("Id", "0") or 0)
            if uid in needed:
                tmp.execute("INSERT OR IGNORE INTO users VALUES(?,?)", (uid, r.get("DisplayName", "")))
    except Exception as e:
        log(f"users skipped for {site}: {e}")
    tmp.commit()
    shard.tick(0.7, "joining questions, answers and authors", force=True)
    # The joined rows go to a scratch table and are read back 1,000 at a time,
    # so a big site (Mathematics: 600,000 threads) never sits in memory whole.
    tmp.execute(
        "CREATE TEMP TABLE joined AS SELECT q.id, q.title, q.tags, q.score, q.body AS qbody,"
        " COALESCE(uq.name, q.oname) AS qauthor, q.lic AS qlic, a.body AS abody,"
        " COALESCE(ua.name, a.oname) AS aauthor, a.lic AS alic, (a.id = q.accepted) AS accepted FROM keep"
        " JOIN q ON q.id = keep.qid JOIN a ON a.id = keep.aid"
        " LEFT JOIN users uq ON uq.id = q.owner LEFT JOIN users ua ON ua.id = a.owner")
    total = tmp.execute("SELECT MAX(rowid) FROM joined").fetchone()[0] or 0
    maths = site in MATH_SITES
    written = 0
    for lo in range(1, total + 1, 1000):
        chunk = tmp.execute("SELECT * FROM joined WHERE rowid BETWEEN ? AND ?", (lo, lo + 999)).fetchall()
        shard.tick(0.7 + 0.3 * written / max(1, total), f"answers: {written:,} of {total:,} written")
        for qid, title, tags, score, qbody, qauthor, qlic, abody, aauthor, alic, accepted in chunk:
            written += 1
            write_thread(shard, site, maths, qid, title, tags, score, qbody, qauthor, qlic, abody, aauthor, alic,
                         accepted)
    return written


def write_thread(shard, site, maths, qid, title, tags, score, qbody, qauthor, qlic, abody, aauthor, alic, accepted):
    """One question with its kept answer, as a document and its passages."""
    if maths:
        title = clean_inline(detex_dollars(title))
    tags = tags.strip("|").replace("|", " ") if tags else ""
    tags = " ".join(re.findall(r"<([^>]+)>", tags)) or tags
    d = shard.add_doc(title, url=f"https://{site}/q/{qid}", author=aauthor or "", license=alic or qlic,
                      tags=tags, score=score, extra=json.dumps({"asker": qauthor or "", "accepted": bool(accepted)}))
    qtext = " ".join(" ".join(t for _, t in tidy_blocks(html_blocks(qbody), maths)).split()[:120])
    shard.add_passage(d, 0, "question", title + ". " + qtext)
    role = "accepted" if accepted else "answer"
    ord_ = 0
    for kind, para in tidy_blocks(html_blocks(abody), maths)[:12]:
        for piece in [para] if kind == "code" else split_long(para):
            ord_ += 1
            shard.add_passage(d, ord_, "code" if kind == "code" else role, piece)


def read_wordnet(path, shard):
    z = zipfile.ZipFile(path)
    synsets = {}
    for name in z.namelist():
        base = os.path.basename(name)
        if base.endswith(".json") and not base.startswith("entries") and base != "frames.json":
            synsets.update(json.loads(z.read(name)))
    posname = {"n": "noun", "v": "verb", "a": "adjective", "s": "adjective", "r": "adverb"}
    n = 0
    entries = [x for x in sorted(z.namelist()) if os.path.basename(x).startswith("entries")]
    for idx, name in enumerate(entries):
        shard.tick(idx / max(1, len(entries)), f"{idx} of {len(entries)} word lists")
        for lemma, by_pos in json.loads(z.read(name)).items():
            word = lemma.replace("_", " ")
            for pos, entry in by_pos.items():
                senses = entry.get("sense", [])[:4]
                if not senses:
                    continue
                d = shard.add_doc(word, url="https://en-word.net/lemma/" + lemma, license="CC BY 4.0",
                                  tags=posname.get(pos, pos))
                for i, s in enumerate(senses):
                    syn = synsets.get(s.get("synset", ""), {})
                    defs = syn.get("definition", [])
                    if not defs:
                        continue
                    text = f"{word} ({posname.get(pos, pos)}): {defs[0]}"
                    ex = syn.get("example", [])
                    if ex:
                        text += ". Example: " + (ex[0] if isinstance(ex[0], str) else ex[0].get("text", ""))
                    members = [m.replace("_", " ") for m in syn.get("members", []) if m != lemma][:4]
                    if members:
                        text += ". Also: " + ", ".join(members)
                    shard.add_passage(d, i, "definition", text + ".")
                n += 1
    return n


def read_wiktionary(path, shard):
    n, lines, seen, size = 0, 0, 0, os.path.getsize(path) or 1
    with open(path, "rb") as f:
        for line in f:
            seen += len(line)
            lines += 1
            if lines % 20000 == 0:
                shard.tick(seen / size, f"{human_bytes(seen)} of {human_bytes(size)}")
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if e.get("lang_code", "en") != "en":
                continue
            word, pos = e.get("word", ""), e.get("pos", "")
            # A sense that only points at another word ("plural of cat",
            # "simple past of run") is stored as a "form", with the word it
            # points at kept in extra, so an answer can follow the link.
            senses, base = [], ""
            for s in e.get("senses", []):
                g = s.get("glosses") or s.get("raw_glosses") or []
                if not g:
                    continue
                tags = s.get("tags", [])
                links = s.get("form_of") or s.get("alt_of") or []
                form = bool(links) or "form-of" in tags or "alt-of" in tags
                if form and not base and links and isinstance(links[0], dict):
                    base = links[0].get("word", "")
                senses.append((clean_inline(g[-1]), form))
                if len(senses) >= 3:
                    break
            if not word or not senses:
                continue
            extra = json.dumps({"etymology": (e.get("etymology_text") or "")[:400], "form_of": base},
                               ensure_ascii=False)
            d = shard.add_doc(word, url="https://en.wiktionary.org/wiki/" + word.replace(" ", "_"),
                              license="CC BY-SA 4.0", tags=pos, extra=extra)
            for i, (g, form) in enumerate(senses):
                shard.add_passage(d, i, "form" if form else "definition", f"{word} ({pos}): {g}")
            n += 1
            if n % 200000 == 0:
                cool_down()
    return n


# Common English words: an excerpt where almost none appear is in another
# language (the corpus has a few bilingual editions: Spanish, Latin, Welsh).
ENGLISH_WORDS = set("""the and of to a in i is that it his he with my for her as was you me on be all not but by their
they at from this so she we no or our thy thou thee shall what when where who like are have had there then will an
which one its into upon now more them than him your o oh ye""".split())


def mostly_english(text):
    w = re.findall(r"[a-z']+", text.lower())
    return bool(w) and sum(x in ENGLISH_WORDS for x in w) >= 0.12 * len(w)


def read_poetry(path, shard):
    catalog = {}
    cat_path = os.path.join(os.path.dirname(path), "pg_catalog.csv")
    if os.path.exists(cat_path):
        with open(cat_path, newline="", encoding="utf-8", errors="replace") as f:
            for row in csv.DictReader(f):
                catalog[row.get("Text#", "")] = (row.get("Title", ""), row.get("Authors", ""))
    books = 0
    cur_gid, lines, doc, chunk_ord = None, [], None, 0

    def flush_chunk(force=False):
        nonlocal lines, chunk_ord
        if doc is not None and lines and (force or len(lines) >= 8):
            text = "\n".join(lines)
            if mostly_english(text):
                shard.add_passage(doc, chunk_ord, "verse", text)
                chunk_ord += 1
            lines = []

    size, count = os.path.getsize(path) or 1, 0
    with gzip.open(path, "rt", encoding="utf-8", errors="replace") as f:
        for raw in f:
            count += 1
            if count % 50000 == 0:
                try:
                    at = f.buffer.fileobj.tell()
                    shard.tick(at / size, f"{human_bytes(at)} of {human_bytes(size)} (compressed)")
                except (AttributeError, OSError, ValueError):
                    pass
            try:
                r = json.loads(raw)
            except ValueError:
                continue
            gid, s = r.get("gid", ""), r.get("s", "")
            if gid != cur_gid:
                flush_chunk(force=True)
                title, authors = catalog.get(gid, ("Project Gutenberg book " + gid, ""))
                author = re.sub(r",\s*\d{3,4}\??-\d{0,4}\??", "", authors.split(";")[0]).strip()
                doc = shard.add_doc(re.split(r"[\r\n]+", title)[0].strip(), url=f"https://www.gutenberg.org/ebooks/{gid}",
                                    author=author, license="Public domain (US)")
                cur_gid, chunk_ord = gid, 0
                books += 1
            lines.append(s)
            if len(lines) >= 16 or (len(lines) >= 8 and s.rstrip().endswith((".", "!", "?"))):
                flush_chunk()
    flush_chunk(force=True)
    return books


def read_documents(folder, shard):
    n = 0
    for path in sorted(glob.glob(os.path.join(folder, "**", "*"), recursive=True)):
        ext = os.path.splitext(path)[1].lower()
        try:
            if ext == ".pdf":
                text = subprocess.run(["pdftotext", "-layout", path, "-"], capture_output=True, text=True,
                                      timeout=300).stdout
            elif ext in (".md", ".markdown", ".rst", ".html", ".htm"):
                text = subprocess.run(["pandoc", "-t", "plain", path], capture_output=True, text=True,
                                      timeout=300).stdout
            elif ext in (".txt", ".text"):
                text = open(path, encoding="utf-8", errors="replace").read()
            else:
                continue
        except Exception as e:
            log(f"skipped {path}: {e}")
            continue
        paras = [re.sub(r"\s+", " ", p).strip() for p in re.split(r"\n\s*\n", text)]
        paras = [p for p in paras if len(p.split()) >= 5]
        if not paras:
            continue
        d = shard.add_doc(os.path.splitext(os.path.basename(path))[0], url="file://" + path, license="local, private")
        for i, p in enumerate(paras):
            for piece in split_long(p):
                shard.add_passage(d, i, "lead" if i == 0 else "body", piece)
        n += 1
    return n


# ---- sniffing and building ----

def sidecar(path):
    folder = path if os.path.isdir(path) else os.path.dirname(path)
    return load_json(os.path.join(folder, "source.json")) or {}


def parquet_has(path, column):
    try:
        import pyarrow.parquet as pq
        return column in pq.ParquetFile(path).schema_arrow.names
    except Exception:
        return False


def sniff(path):
    """(name, kind, inputs, reader, detail, attribution, license, source, version) for a path, or None."""
    path = os.path.abspath(path)
    sc = sidecar(path)
    base = os.path.basename(path.rstrip("/"))
    if os.path.isdir(path):
        pq_files = sorted(glob.glob(os.path.join(path, "*.parquet")))
        if pq_files:
            simple = "simple" in (sc.get("version", "") + path)
            finewiki = sc.get("source") == "HuggingFaceFW/finewiki" or parquet_has(pq_files[0], "wikiname")
            name = sc.get("name") or ("wikipedia-simple" if simple else "wikipedia-en")
            return dict(name=name, kind="encyclopedia", inputs=pq_files, reader="finewiki" if finewiki else "wikipedia",
                        detail="full" if simple else "column",
                        attribution='From Wikipedia, "{title}"', license=sc.get("license", "CC BY-SA 4.0"),
                        source=sc.get("source", "HuggingFaceFW/finewiki" if finewiki else "wikimedia/wikipedia"),
                        version=sc.get("version", base))
        if glob.glob(os.path.join(path, "*.7z")):
            return None  # a folder of StackExchange sites: add each archive on its own
        return dict(name="docs-" + title_key(base).replace(" ", "-"), kind="documents", inputs=[path],
                    reader="documents", detail="full", attribution='From your file "{title}"',
                    license="local, private", source=path, version=time.strftime("%Y%m%d%H%M"))
    if base.endswith(".parquet"):
        return sniff(os.path.dirname(path))
    if base.endswith(".7z") and ("stackexchange" in base or base in ("askubuntu.com.7z", "superuser.com.7z")):
        site = base[:-3]
        short = site.replace(".stackexchange.com", "").replace(".com", "")
        return dict(name="se-" + short, kind="qa", inputs=[path], reader="stackexchange", detail="full", site=site,
                    attribution='From ' + site + ', "{title}"', license="CC BY-SA (per post)",
                    source="archive.org/stackexchange", version=sc.get("version", time.strftime("%Y-%m", time.localtime(os.path.getmtime(path)))))
    if base.startswith("english-wordnet") and base.endswith(".zip"):
        return dict(name="wordnet-en", kind="definitions", inputs=[path], reader="wordnet", detail="full",
                    attribution="From WordNet", license="CC BY 4.0", source="globalwordnet/english-wordnet",
                    version=re.sub(r"\D", "", base)[:4] or sc.get("version", "1"))
    if base.startswith("kaikki") and base.endswith(".jsonl"):
        return dict(name="wiktionary-en", kind="definitions", inputs=[path], reader="wiktionary", detail="full",
                    attribution="From Wiktionary", license="CC BY-SA 4.0 and GFDL", source="kaikki.org",
                    version=sc.get("version", time.strftime("%Y-%m", time.localtime(os.path.getmtime(path)))))
    if base.startswith("gutenberg-poetry") and base.endswith(".ndjson.gz"):
        return dict(name="gutenberg-poetry", kind="poems", inputs=[path], reader="poetry", detail="full",
                    attribution="From {title} by {author} (Project Gutenberg)", license="Public domain (US)",
                    source="gutenberg-poetry-corpus", version=sc.get("version", "v001"))
    return None


def manifest_path(folder):
    return os.path.join(folder, "manifest.json")


def read_manifest(folder):
    return load_json(manifest_path(folder))


def write_manifest(folder, m):
    tmp = manifest_path(folder) + ".tmp"
    with open(tmp, "w") as f:
        json.dump(m, f, indent=1)
    os.replace(tmp, manifest_path(folder))


def build_shard(spec, i, inp, out_path, meta, shards=1):
    cool_down()
    detail = spec["detail"]
    shard = Shard(out_path + ".tmp", dict(meta, shard=i, input=os.path.basename(inp)), detail)
    shard.ticker = Ticker(spec["name"], i, shards)
    try:
        n, docs, passages = read_into(spec, inp, shard)
    finally:
        shard.ticker.done()
    os.replace(out_path + ".tmp", out_path)
    return n, docs, passages


def read_into(spec, inp, shard):
    r = spec["reader"]
    if r == "finewiki":
        n = read_finewiki_file(inp, shard)
    elif r == "wikipedia":
        n = read_wikipedia_file(inp, shard)
    elif r == "stackexchange":
        n = read_stackexchange(inp, shard, spec["site"])
    elif r == "wordnet":
        n = read_wordnet(inp, shard)
    elif r == "wiktionary":
        n = read_wiktionary(inp, shard)
    elif r == "poetry":
        n = read_poetry(inp, shard)
    else:
        n = read_documents(inp, shard)
    docs, passages = shard.docs, shard.passages
    shard.finish()
    return n, docs, passages


# ---- checking a build before it is installed ----
# Each pattern is a kind of damage seen in real sources: a number the source
# lost ("It is about  high"), brackets emptied by removed markup, HTML or
# wiki markup that survived, characters broken by a wrong encoding, formulas
# left as LaTeX. Code passages are not measured (code legitimately has "()").

DEFECTS = {
    "number holes": re.compile(r"\b(?:about|around|nearly|over|under|approximately|roughly|almost|than|at|is|was|by)"
                               r"  +[a-z(]"),
    "empty brackets": re.compile(r"\(\s*[,;]?\s*\)"),
    "leftover markup": re.compile(r"</?(?:p|div|span|a|br|code|pre|strong|em|img|li|ul|ol|table|td|tr)\b[^>]*>|"
                                  r"&(?:amp|lt|gt|quot|nbsp|#\d+);|\{\{|\}\}|\[\[|\]\]|\|thumb\||<ref\b"),
    "broken characters": re.compile("\u00c3[\u0080-\u00bf]|\u00e2\u20ac|\ufffd"),
    "raw formulas": re.compile(r"\\(?:frac|displaystyle|mathrm|sqrt|cdot|times|left|right|alpha|sum|int)\b"),
    "tiny passages": re.compile(r"^.{0,14}$", re.S),
}
# Above these rates (percent of passages) a build is not installed.
LIMITS = {"number holes": 3.0, "empty brackets": 3.0, "leftover markup": 2.0, "broken characters": 1.0}


def quality(shard_paths, sample=6000, seed=7):
    """(passages checked, {defect: percent}, {defect: example}) over a random sample."""
    rng = random.Random(seed)
    counts = {k: 0 for k in DEFECTS}
    examples = {}
    n = 0
    per = max(300, sample // max(1, len(shard_paths)))
    for p in shard_paths:
        try:
            db = sqlite3.connect(f"file:{p}?mode=ro", uri=True)
            top = db.execute("SELECT MAX(pid) FROM passages").fetchone()[0] or 0
        except sqlite3.Error:
            continue
        ids = rng.sample(range(1, top + 1), min(per, top)) if top else []
        for i in range(0, len(ids), 500):
            chunk = ids[i:i + 500]
            sql = f"SELECT text, role FROM passages WHERE pid IN ({','.join('?' * len(chunk))})"
            for text, role in db.execute(sql, chunk):
                if role == "code":
                    continue
                n += 1
                for k, rx in DEFECTS.items():
                    if rx.search(text):
                        counts[k] += 1
                        examples.setdefault(k, text[:200])
        db.close()
    rates = {k: round(100.0 * v / n, 2) for k, v in counts.items()} if n else {}
    return n, rates, examples


def check(name=None):
    if not os.path.isdir(PACKS):
        print("no packs installed")
        return
    for d in sorted(os.listdir(PACKS)):
        folder = os.path.join(PACKS, d)
        m = read_manifest(folder)
        if (name and d != name) or not m or d.endswith((".building", ".previous")):
            continue
        n, rates, examples = quality([os.path.join(folder, s["file"]) for s in m.get("shards", [])])
        found = [f"{k} {v:.1f}%" for k, v in sorted(rates.items(), key=lambda kv: -kv[1]) if v >= 0.1]
        over = [k for k, lim in LIMITS.items() if rates.get(k, 0) > lim]
        stale = "" if m.get("ingest_version") == INGEST_VERSION else "  (built by an older ingest: rebuild it)"
        print(f"{d:24s} {n:6d} passages checked  {', '.join(found) or 'clean'}{'  OVER LIMIT' if over else ''}{stale}")
        if name:
            for k, e in examples.items():
                print(f"    {k}: {e!r}")


def add(path, force=False, name=None, workers=2):
    spec = sniff(path)
    if not spec:
        log(f"not a recognized source: {path}")
        return False
    if name:
        spec["name"] = name
    os.makedirs(PACKS, exist_ok=True)
    final = os.path.join(PACKS, spec["name"])
    old = read_manifest(final)
    if old and not force and str(old.get("version", "")) >= str(spec["version"]) and \
            old.get("ingest_version") == INGEST_VERSION:
        log(f"{spec['name']}: version {old.get('version')} already installed, skipped")
        return False
    free = shutil.disk_usage(os.path.dirname(PACKS)).free
    need = sum(os.path.getsize(p) if os.path.isfile(p) else 0 for p in spec["inputs"]) * 2.5
    if need and free < need:
        log(f"{spec['name']}: not enough disk space ({free >> 30} GB free, ~{int(need) >> 30} GB needed)")
        return False
    building = final + ".building"
    m = read_manifest(building)
    if os.path.isdir(building) and (not m or m.get("version") != spec["version"] or
                                    m.get("ingest_version") != INGEST_VERSION):
        shutil.rmtree(building)  # a half build of another version or by an older ingest
        m = None
    os.makedirs(building, exist_ok=True)
    if not m:
        m = {"format": "unllm-pack/1", "name": spec["name"], "kind": spec["kind"], "source": spec["source"],
             "version": spec["version"], "license": spec["license"], "attribution": spec["attribution"],
             "ingest_version": INGEST_VERSION, "shards": []}
    meta = {k: m[k] for k in ("name", "kind", "source", "version", "license", "attribution")}
    done = {s["input"] for s in m["shards"] if s.get("status") == "done"}
    todo = [(i, inp) for i, inp in enumerate(spec["inputs"]) if os.path.basename(inp) not in done]
    log(f"{spec['name']}: building {len(todo)} of {len(spec['inputs'])} shard(s)")
    start = time.time()
    status = os.path.join(PROGRESS, f"{spec['name']}.json")
    info = {"pack": spec["name"], "shards": len(spec["inputs"]), "shards_done": len(done), "phase": "building",
            "started": start, "pid": os.getpid()}
    save_json(status, info)
    jobs = []
    if workers > 1 and len(todo) > 1:
        from concurrent.futures import ProcessPoolExecutor
        with ProcessPoolExecutor(max_workers=workers) as ex:
            for i, inp in todo:
                out = os.path.join(building, f"shard-{i:03d}.sqlite")
                jobs.append((i, inp, out, ex.submit(build_shard, spec, i, inp, out, meta, len(spec["inputs"]))))
            for i, inp, out, fut in jobs:
                n, docs, passages = fut.result()
                m["shards"].append({"file": os.path.basename(out), "input": os.path.basename(inp), "docs": docs,
                                    "passages": passages, "status": "done"})
                write_manifest(building, m)
                info["shards_done"] += 1
                save_json(status, info)
                log(f"{spec['name']}: shard {i} done ({docs} docs, {passages} passages)")
    else:
        for i, inp in todo:
            out = os.path.join(building, f"shard-{i:03d}.sqlite")
            n, docs, passages = build_shard(spec, i, inp, out, meta, len(spec["inputs"]))
            m["shards"].append({"file": os.path.basename(out), "input": os.path.basename(inp), "docs": docs,
                                "passages": passages, "status": "done"})
            write_manifest(building, m)
            info["shards_done"] += 1
            save_json(status, info)
            log(f"{spec['name']}: shard {i} done ({docs} docs, {passages} passages)")
    m["docs"] = sum(s["docs"] for s in m["shards"])
    m["passages"] = sum(s["passages"] for s in m["shards"])
    m["built"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    m["built_seconds"] = round(time.time() - start)
    info["phase"] = "checking"
    save_json(status, info)
    n, rates, examples = quality([os.path.join(building, s["file"]) for s in m["shards"]])
    m["quality"] = {"passages_checked": n, "defect_percent": rates}
    write_manifest(building, m)
    found = ", ".join(f"{k} {v:.1f}%" for k, v in rates.items() if v >= 0.1)
    log(f"{spec['name']}: checked {n} passages: {found or 'no defects found'}")
    bad = [f"{k} {rates.get(k, 0):.1f}% (limit {lim}%)" for k, lim in LIMITS.items() if rates.get(k, 0) > lim]
    if not m["docs"]:
        bad.append("no documents")
    if bad and spec["kind"] != "documents" and not force:
        log(f"{spec['name']}: NOT installed, the text is too damaged: {'; '.join(bad)}. "
            f"The build stays in {building}; add --force to install it anyway.")
        for k in LIMITS:
            if k in examples and rates.get(k, 0) > LIMITS[k]:
                log(f"{spec['name']}:   e.g. {k}: {examples[k]!r}")
        drop_file(status)
        return False
    previous = final + ".previous"
    if os.path.exists(previous):
        shutil.rmtree(previous)
    if os.path.exists(final):
        os.rename(final, previous)
    os.rename(building, final)
    if os.path.exists(previous):
        shutil.rmtree(previous)
    drop_file(status)
    log(f"{spec['name']}: installed, {m['docs']} docs, {m['passages']} passages, {time.time() - start:.0f} s")
    learned = os.path.join(DATA, "learned")
    os.makedirs(learned, exist_ok=True)
    with open(os.path.join(learned, "new-packs.txt"), "a") as f:
        f.write(f"{int(time.time())}\t{spec['name']}\n")
    return True


def inbox(night_only=False, max_hours=6.0):
    os.makedirs(INBOX, exist_ok=True)
    started = time.time()
    for entry in sorted(os.listdir(INBOX)):
        if entry == "done":
            continue
        p = os.path.join(INBOX, entry)
        target = p
        if entry.endswith(".source"):
            target = open(p).read().strip()
        elif os.path.islink(p):
            target = os.path.realpath(p)
        size = os.path.getsize(target) if os.path.isfile(target) else sum(
            os.path.getsize(f) for f in glob.glob(os.path.join(target, "**", "*"), recursive=True) if os.path.isfile(f))
        hour = time.localtime().tm_hour
        if night_only and size > 300 * 1024 * 1024 and not (1 <= hour < 7):
            log(f"inbox: {entry} is large, waiting for the night window")
            continue
        if time.time() - started > max_hours * 3600:
            log("inbox: time budget used, the rest waits for the next run")
            break
        if os.path.isdir(target) and glob.glob(os.path.join(target, "*.7z")):
            ok = all(add(a) is not None for a in sorted(glob.glob(os.path.join(target, "*.7z"))))
        else:
            ok = add(target)
        done = os.path.join(INBOX, "done", time.strftime("%Y-%m-%d"))
        os.makedirs(done, exist_ok=True)
        if os.path.islink(p) or entry.endswith(".source"):
            os.replace(p, os.path.join(done, entry))
        elif ok is not None:
            shutil.move(p, os.path.join(done, entry))


# ---- progress view ----

def input_bytes(spec):
    total = 0
    for p in spec["inputs"]:
        if os.path.isfile(p):
            total += os.path.getsize(p)
        else:
            total += sum(os.path.getsize(f) for f in glob.glob(os.path.join(p, "**", "*"), recursive=True)
                         if os.path.isfile(f))
    return total


def plan_item(path, name=None):
    spec = sniff(path)
    if not spec:
        return None
    return {"name": name or spec["name"], "path": os.path.abspath(path), "version": spec["version"],
            "bytes": input_bytes(spec)}


def discover(root=None):
    """Every source in the download folder that the builder recognizes."""
    root = root or KNOWLEDGE
    items = []
    if not os.path.isdir(root):
        return items
    for d in sorted(os.listdir(root)):
        p = os.path.join(root, d)
        if not os.path.isdir(p):
            continue
        if glob.glob(os.path.join(p, "*.parquet")):
            paths = [p]
        elif glob.glob(os.path.join(p, "*.7z")):
            paths = sorted(glob.glob(os.path.join(p, "*.7z")))
        else:
            paths = [os.path.join(p, f) for f in sorted(os.listdir(p)) if os.path.isfile(os.path.join(p, f))]
        for path in paths:
            it = plan_item(path)
            if it and sniff(path)["kind"] != "documents":
                items.append(it)
    return items


def packs_being_built():
    """Names of the packs a running builder has a file open for. The builder
    keeps its shard file open from start to finish, whatever phase it is in."""
    names = set()
    prefix = os.path.abspath(PACKS) + os.sep
    for cmd in glob.glob("/proc/[0-9]*/cmdline"):
        try:
            with open(cmd, "rb") as f:
                if b"unllm_ingest.py" not in f.read():
                    continue
            fds = os.path.join(os.path.dirname(cmd), "fd")
            for fd in os.listdir(fds):
                try:
                    target = os.readlink(os.path.join(fds, fd))
                except OSError:
                    continue
                if target.startswith(prefix):
                    top = target[len(prefix):].split(os.sep, 1)[0]
                    if top.endswith(".building"):
                        names.add(top[:-len(".building")])
        except OSError:
            continue
    return names


_MEMBER_SIZES = {}


def unpack_progress(archive):
    """For a build that writes no progress files: which file its 7z unpacker
    is extracting, and how much of that file it has written out so far."""
    target = os.fsencode(os.path.abspath(archive))
    for cmd in glob.glob("/proc/[0-9]*/cmdline"):
        try:
            with open(cmd, "rb") as f:
                args = f.read().split(b"\x00")
            if len(args) < 5 or not args[0].endswith(b"7z") or args[1] != b"e" or target not in args:
                continue
            member = args[args.index(target) + 1].decode(errors="replace")
            key = (archive, member)
            if key not in _MEMBER_SIZES:
                _MEMBER_SIZES[key] = member_size(archive, member)
            with open(os.path.join(os.path.dirname(cmd), "io")) as f:
                written = int(next(line.split()[1] for line in f if line.startswith("wchar")))
            return member, min(1.0, written / _MEMBER_SIZES[key]) if _MEMBER_SIZES[key] else 0.0
        except (OSError, StopIteration, ValueError, IndexError):
            continue
    return None


def collect(items, since=None):
    """(states, overall fraction, seconds left or None) for planned items.
    A state is {"state": done|building|waiting|refused, "fraction", "detail"}.
    since: when this run of builds started; time left comes from how fast
    this run has actually gone (data finished per second so far)."""
    now = time.time()
    try:
        with open(LOG) as f:
            log_tail = f.readlines()[-400:]
    except OSError:
        log_tail = []
    active = packs_being_built()
    states = []
    run_bytes, own_rate = 0.0, None
    for it in items:
        name = it["name"]
        m = read_manifest(os.path.join(PACKS, name))
        pack = load_json(os.path.join(PROGRESS, f"{name}.json"))
        if pack and alive(pack.get("pid")):
            shards = max(1, int(pack.get("shards", 1)))
            parts = [load_json(f) for f in glob.glob(os.path.join(PROGRESS, f"{glob.escape(name)}.shard-*.json"))]
            parts = sorted((x for x in parts if x and alive(x.get("pid"))), key=lambda x: x.get("shard", 0))

            def say(x):
                d = x.get("detail", "")
                if x.get("phase") in ("indexing", "compacting") and x.get("phase_started"):
                    d += f" ({human_time(now - x['phase_started'])} so far)"
                return d

            frac = min(1.0, (pack.get("shards_done", 0) + sum(x.get("fraction", 0.0) for x in parts)) / shards)
            if pack.get("phase") == "checking":
                frac, detail = max(frac, 0.99), "checking the text"
            elif shards > 1:
                detail = f"{pack.get('shards_done', 0)} of {shards} parts done; " + "; ".join(
                    f"part {x['shard'] + 1}: {say(x)}" for x in parts)
            else:
                detail = say(parts[0]) if parts else "starting"
            elapsed = now - pack.get("started", now)
            if frac > 0.01 and elapsed > 30:
                detail += f", about {human_time(elapsed * (1 - frac) / frac)} left"
                own_rate = it["bytes"] * frac / elapsed
            run_bytes += it["bytes"] * frac
            states.append({"state": "building", "fraction": frac, "detail": detail})
            continue
        fresh = bool(m) and m.get("ingest_version") == INGEST_VERSION and \
            str(m.get("version", "")) >= str(it.get("version") or m.get("version", ""))
        refused = not fresh and any(f"{name}: NOT installed" in line for line in log_tail)
        if fresh:
            built = m.get("built", "")
            if since and built and time.mktime(time.strptime(built, "%Y-%m-%dT%H:%M:%S")) >= since:
                run_bytes += it["bytes"]
            states.append({"state": "done", "fraction": 1.0, "detail": f"installed {built[11:16]}".strip()})
        elif refused:
            states.append({"state": "refused", "fraction": 0.0, "detail": "not installed: text too damaged (see log)"})
        elif name in active:
            up = unpack_progress(it["path"]) if it["path"].endswith(".7z") else None
            if up:
                member, f = up
                frac = 0.85 * (0.6 * f if member == "Posts.xml" else 0.6 + 0.1 * f)
                states.append({"state": "building", "fraction": frac,
                               "detail": f"unpacking {member}: {f * 100:.0f}% (from the unpacker)"})
            else:
                folder = os.path.join(PACKS, name + ".building")
                wrote = sum(os.path.getsize(f) for f in glob.glob(os.path.join(glob.escape(folder), "*"))
                            if os.path.isfile(f))
                states.append({"state": "building", "fraction": None,
                               "detail": f"writing and indexing: {human_bytes(wrote)} written so far"})
        else:
            states.append({"state": "waiting", "fraction": 0.0,
                           "detail": "an older copy is installed" if m else ""})
    total = sum(it["bytes"] for it in items) or 1
    done_bytes = sum(it["bytes"] * (st["fraction"] or 0.0) for it, st in zip(items, states))
    overall = done_bytes / total
    left = None
    if overall < 1:
        if since and now - since > 60 and run_bytes > 0:
            left = (total - done_bytes) / (run_bytes / (now - since))
        elif own_rate:
            left = (total - done_bytes) / own_rate
    return states, overall, left


def render(items, states, overall, left, width=100, color=False, size=human_bytes):
    bw = max(10, min(30, width - 70))

    def bar(f):
        if f is None:
            return "▒" * bw
        n = int(round(max(0.0, min(1.0, f)) * bw))
        return "█" * n + "░" * (bw - n)

    paint = {"done": "\033[32m", "building": "\033[1;33m", "waiting": "\033[2m", "refused": "\033[31m"}
    total = sum(it["bytes"] for it in items)
    done_bytes = sum(it["bytes"] * (st["fraction"] or 0.0) for it, st in zip(items, states))
    finished = sum(1 for st in states if st["state"] == "done")
    out = [f"unllm pack builds   {time.strftime('%H:%M:%S')}", ""]
    tail = f", about {human_time(left)} left" if left else ""
    out.append(f"All   {bar(overall)} {overall * 100:3.0f}%  {finished} of {len(items)} tasks done, "
               f"{size(done_bytes)} of {size(total)}{tail}")
    out.append("")
    for it, st in zip(items, states):
        label = {"done": "done", "building": "NOW", "waiting": "waiting", "refused": "REFUSED"}[st["state"]]
        pct = "  ? " if st["fraction"] is None else f"{st['fraction'] * 100:3.0f}%"
        line = f"  {label:<8}{it['name']:<20.20} {size(it['bytes']):>8}  {bar(st['fraction'])} {pct}  {st['detail']}"
        line = line[:width]
        out.append(paint[st["state"]] + line + "\033[0m" if color else line)
    return "\n".join(out)


_TABLE_BYTES = {}


def passages_table_bytes(shard):
    """How many bytes of a shard file the passages table takes: what the
    search indexer reads from that shard."""
    key = (shard, os.path.getmtime(shard))
    if key not in _TABLE_BYTES:
        cache = os.path.join(PROGRESS, "table-bytes.json")
        known = load_json(cache) or {}
        k = f"{shard}:{key[1]:.0f}"
        if k not in known:
            try:
                db = sqlite3.connect(f"file:{shard}?mode=ro", uri=True)
                known[k] = db.execute("SELECT SUM(pgsize) FROM dbstat WHERE name = 'passages'").fetchone()[0] or 0
                db.close()
            except sqlite3.Error:
                known[k] = os.path.getsize(shard)
            save_json(cache, known)
        _TABLE_BYTES[key] = known[k]
    return _TABLE_BYTES[key]


def indexers_running():
    """{pack name: pid} for every running search indexer (unllm_index)."""
    out = {}
    for cmd in glob.glob("/proc/[0-9]*/cmdline"):
        try:
            with open(cmd, "rb") as f:
                args = f.read().split(b"\x00")
        except OSError:
            continue
        if args and os.path.basename(args[0]) == b"unllm_index" and len(args) > 1:
            out[os.path.basename(os.path.normpath(args[1].decode(errors="replace")))] = int(cmd.split("/")[2])
    return out


def index_states(running):
    """(items, states) for the search index builds, one per pack, sized by
    passages; a building state also has "left" (seconds) when known."""
    now = time.time()
    items, states = [], []
    for name in sorted(os.listdir(PACKS)):
        d = os.path.join(PACKS, name)
        m = read_manifest(d)
        if not m or "." in name:
            continue
        n = int(m.get("passages", 0))
        items.append({"name": name, "bytes": n})
        if name in running:
            pid = running[name]
            rep = load_json(os.path.join(PROGRESS, f"index-{name}.json"))
            if rep and rep.get("pid") == pid and rep.get("total"):
                frac = rep["done"] / rep["total"]
                if rep.get("phase") == "merging":
                    frac, detail = 0.99, "merging the index files"
                else:
                    detail = f"part {rep['shard'] + 1} of {rep['shards']}, {rep['done']:,} passages"
                started = rep.get("started", now)
            else:  # an indexer from before progress reports: bytes it has read
                shards = sorted(glob.glob(os.path.join(glob.escape(d), "shard-*.sqlite")))
                try:
                    with open(f"/proc/{pid}/io") as f:
                        read = int(next(x.split()[1] for x in f if x.startswith("rchar")))
                    opened = [os.path.basename(os.readlink(f"/proc/{pid}/fd/{fd}")) for fd in os.listdir(f"/proc/{pid}/fd")]
                    started = os.stat(f"/proc/{pid}").st_mtime
                except (OSError, StopIteration, ValueError):
                    read, opened, started = 0, [], now
                need = sum(passages_table_bytes(s) for s in shards) or 1
                frac = min(0.99, read / need)
                part = next((int(o[6:9]) for o in opened if o.startswith("shard-")), None)
                detail = f"part {part + 1} of {len(shards)}" if part is not None else "merging the index files"
            elapsed = now - started
            left = elapsed * (1 - frac) / frac if 0.01 < frac < 0.99 and elapsed > 20 else None
            if left:
                detail += f", about {human_time(left)} left"
            states.append({"state": "building", "fraction": frac, "detail": detail, "left": left})
        elif os.path.isdir(os.path.join(d, "tantivy")):
            states.append({"state": "done", "fraction": 1.0, "detail": "indexed"})
        else:
            states.append({"state": "waiting", "fraction": 0.0, "detail": ""})
    return items, states


def index_view(running, width, color):
    """The search index builds: one line per pack, sized by passages."""
    items, states = index_states(running)
    total = sum(it["bytes"] for it in items) or 1
    overall = sum(it["bytes"] * st["fraction"] for it, st in zip(items, states)) / total
    text = render(items, states, overall, None, width, color, size=passages_count)
    return text.replace("unllm pack builds", "unllm search index builds", 1)


def passages_count(n):
    return f"{n / 1e6:.1f} M" if n >= 1e6 else f"{n / 1e3:.0f} k"


def task_states():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # tools/taskprogress.py
    from taskprogress import task_states as states
    return states()


def tasks_view(width, color):
    """Other long tasks (measurements, sweeps) that write task-*.json."""
    bw = max(10, min(30, width - 60))
    out = []
    for t in task_states():
        if not t["alive"]:
            continue
        frac = t["fraction"]
        n = int(round(frac * bw))
        bar = "\u2588" * n + "\u2591" * (bw - n)
        left = f", about {human_time(t['left'])} left" if t["left"] else ""
        line = f"{t['title']:<20.20} {bar} {frac * 100:3.0f}%  {t['done']} of {t['total']}{left}"
        detail = t["detail"]
        text = line[:width] + ("\n" + ("  " + detail)[:width] if detail else "")
        out.append("\033[1;33m" + text + "\033[0m" if color else text)
    return ("unllm tasks   " + time.strftime("%H:%M:%S") + "\n\n" + "\n\n".join(out)) if out else ""


def progress_view(watch=False):
    me = os.path.abspath(__file__)
    stamp = os.path.getmtime(me)
    while True:
        if watch and os.path.getmtime(me) != stamp:
            time.sleep(1)  # let the edit finish, then start again with the new code
            os.execv(sys.executable, [sys.executable] + (["-I"] if sys.flags.isolated else []) + [me] + sys.argv[1:])
        running = indexers_running()
        width = shutil.get_terminal_size((110, 40)).columns
        tasks = tasks_view(width, sys.stdout.isatty())
        if running or tasks:
            text = "\n\n".join(x for x in (tasks, index_view(running, width, sys.stdout.isatty()) if running else "")
                                 if x)
            if not watch:
                print(text)
                return
            note = "sizes are passages, M = million; " if running else ""
            print("\033[H\033[2J" + text + f"\n\n({note}refreshes every 2 s; Ctrl-C to stop)", flush=True)
            try:
                time.sleep(2)
            except KeyboardInterrupt:
                return
            continue
        q = load_json(QUEUE)
        if q and alive(q.get("pid")):
            items, since = q.get("items", []), q.get("started")
        else:
            items, since = discover(), None
        states, overall, left = collect(items, since)
        width = shutil.get_terminal_size((110, 40)).columns
        text = render(items, states, overall, left, width, color=sys.stdout.isatty())
        if not watch:
            print(text)
            return
        print("\033[H\033[2J" + text + "\n\n(refreshes every 2 s; Ctrl-C to stop)", flush=True)
        try:
            time.sleep(2)
        except KeyboardInterrupt:
            return


def list_packs():
    if not os.path.isdir(PACKS):
        print("no packs installed")
        return
    for name in sorted(os.listdir(PACKS)):
        m = read_manifest(os.path.join(PACKS, name))
        if m:
            print(f"{name:24s} {m.get('kind', ''):13s} {m.get('version', ''):18s} "
                  f"{m.get('docs', 0):>9} docs {m.get('passages', 0):>10} passages  {m.get('license', '')}")
        elif not name.endswith((".building", ".previous")):
            print(f"{name:24s} (no manifest)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add")
    a.add_argument("paths", nargs="+")
    a.add_argument("--name")
    a.add_argument("--force", action="store_true")
    a.add_argument("--workers", type=int, default=2)
    i = sub.add_parser("inbox")
    i.add_argument("--night-only", action="store_true")
    i.add_argument("--max-hours", type=float, default=6.0)
    pr = sub.add_parser("progress")
    pr.add_argument("--watch", action="store_true")
    sub.add_parser("list")
    c = sub.add_parser("check")
    c.add_argument("name", nargs="?")
    r = sub.add_parser("remove")
    r.add_argument("name")
    args = ap.parse_args()
    os.nice(10) if os.nice(0) < 10 else None
    if args.cmd == "add":
        targets = []
        for p in args.paths:
            if os.path.isdir(p) and glob.glob(os.path.join(p, "*.7z")):
                targets += sorted(glob.glob(os.path.join(p, "*.7z")))
            else:
                targets.append(p)
        name = args.name if len(targets) == 1 else None
        plan = [it for it in (plan_item(t, name) for t in targets) if it]
        if len(plan) > 1:
            save_json(QUEUE, {"started": time.time(), "pid": os.getpid(), "items": plan})
        try:
            for t in targets:
                add(t, args.force, name, args.workers)
        finally:
            q = load_json(QUEUE)
            if q and q.get("pid") == os.getpid():
                drop_file(QUEUE)
    elif args.cmd == "progress":
        progress_view(args.watch)
    elif args.cmd == "inbox":
        inbox(args.night_only, args.max_hours)
    elif args.cmd == "list":
        list_packs()
    elif args.cmd == "check":
        check(args.name)
    elif args.cmd == "remove":
        p = os.path.join(PACKS, args.name)
        if os.path.isdir(p):
            shutil.rmtree(p)
            log(f"{args.name}: removed")
        else:
            print("no such pack:", args.name)


if __name__ == "__main__":
    main()
