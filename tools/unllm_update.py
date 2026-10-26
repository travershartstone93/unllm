#!/usr/bin/env python3
"""Keep the knowledge packs and their vector index current.

  unllm_update.py check [--json]       ask each source whether newer data exists than what is installed; no download
  unllm_update.py run [--dry-run] [--full] [--source NAME]...
                                       fetch the sources that are newer, build their packs beside the old ones, embed
                                       their passages, patch the index, verify, swap; the old set is kept seven days
  unllm_update.py run --repair [--dry-run]
                                       re-embed every shard whose entries in the index disagree with its pack
  unllm_update.py prune                delete the previous set now
  unllm_update.py rollback             put the previous set back (while it still exists)

The check asks each source's server for the current version of its files (a few small requests, never the files
themselves) and compares that with what the packs were built from:

  Wikipedia (FineWiki on Hugging Face)  the published checksum of every data file, against the checksums the download
                                        script verified (kept in the download folder, and copied into the state folder
                                        here so the downloads can be deleted afterwards)
  StackExchange (archive.org)           each site dump's date, size and checksum on the archive
  Wiktionary (kaikki.org)               the file's date and size on the server
  WordNet (GitHub)                      the latest release's edition
  Gutenberg poetry                      a fixed file; never changes

An update touches nothing live until the end. New packs are built under <data>/staging/packs with the ingest tool;
their passages are embedded with unllm_embed into code files; a new index is written beside the old one with those
shards replaced (the trained part of the index is kept, so everything untouched searches exactly as before); the
candidate set is scored on the verification questions against the live set and only goes live when it is not worse;
then packs and index are swapped, the resident unllm is stopped so the next question starts on the new set, and the
old set waits under <data>/previous for seven days (sooner when the disk is short: under 80 GB free at the start, it
goes as soon as the new set passes). Raw downloads are not kept.

What is installed is read from the packs' own manifests (<data>/packs/*/manifest.json) and, when still present, the
download folder's source notes (~/Downloads/unllm-knowledge/*/source.json). State lives in <data>/update/state.json.
Exit codes for check: 0 current, 1 something newer, 2 a source could not be reached. For run: 0 done (or nothing to
do), 1 not done (the candidate was worse, or a step failed).

All requests go through curl over IPv4 (huggingface.co over IPv6 does not answer from here). <data> is
~/.local/share/unllm (UNLLM_DATA overrides).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time

HOME = os.path.expanduser("~")
DATA = os.environ.get("UNLLM_DATA") or os.path.join(os.environ.get("XDG_DATA_HOME", HOME + "/.local/share"), "unllm")
PACKS = os.environ.get("UNLLM_PACKS") or os.path.join(DATA, "packs")
DOWNLOADS = os.environ.get("UNLLM_KNOWLEDGE") or os.path.join(HOME, "Downloads/unllm-knowledge")
STATE_DIR = os.path.join(DATA, "update")
STATE = os.path.join(STATE_DIR, "state.json")
STAGING = os.path.join(DATA, "staging")
PREVIOUS = os.path.join(DATA, "previous")
VECTORS = os.path.join(DATA, "vectors")
MODEL = os.environ.get("UNLLM_EMBED") or os.path.join(DATA, "models/bge-small-en-v1.5")
TOOLS = os.path.dirname(os.path.abspath(__file__))
KEEP_DAYS = 7
LOW_DISK = 80 << 30

SE_SITES = ["cooking", "diy", "gardening", "travel", "outdoors", "bicycles", "pets", "money", "lifehacks", "health",
            "english", "ell", "physics", "chemistry", "biology", "astronomy", "history", "mechanics", "unix",
            "electronics", "math"]
SE_FILES = [f"{s}.stackexchange.com.7z" for s in SE_SITES] + ["askubuntu.com.7z", "superuser.com.7z"]
WIKTIONARY_URL = "https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl"
HF = "https://huggingface.co"


# ---------------------------------------------------------------- small helpers

def curl(url, head=False, timeout=25):
    """The body of a GET (or the headers of a HEAD) as text, "" when the server could not be reached."""
    cmd = ["curl", "-4", "-s", "-L", "-m", str(timeout), "--retry", "2"] + (["-I"] if head else []) + [url]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 10)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return out.stdout if out.returncode == 0 else ""


def load_json(path, default=None):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return default


def load_json_text(text):
    try:
        return json.loads(text)
    except (ValueError, TypeError):
        return None


def save_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=1, sort_keys=True)
    os.replace(tmp, path)


def human(n):
    n = float(n)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1000 or unit == "TB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1000


def hours(seconds):
    return f"{seconds / 60:.0f} min" if seconds < 5400 else f"{seconds / 3600:.1f} h"


def day(epoch):
    return time.strftime("%Y-%m-%d", time.localtime(epoch))


def log(msg):
    print(msg, flush=True)


class Bar:
    """The tray's progress item, with an absolute set() on top of the helper's step()."""

    def __init__(self, task):
        self.task, self.done = task, 0

    def step(self, n=1, detail=None):
        self.done += n
        self.task.step(n, detail=detail)

    def set(self, value, detail=None):
        if value > self.done:
            self.step(value - self.done, detail)
        elif detail:
            self.task.note(detail)

    def finish(self):
        self.task.finish()


def bar_start(name, title, total):
    try:
        sys.path.insert(0, os.path.join(HOME, ".local/lib/taskprogress"))
        from taskprogress import Task
        return Bar(Task(name, title, total))
    except Exception:  # the tray helper is this machine's; everything works without it
        return None


def tool(name):
    """A sibling program (unllm_embed, the unllm CLI): UNLLM_BIN, beside the installed unllm, the build folder."""
    places = [os.environ.get("UNLLM_BIN", "")]
    which = shutil.which("unllm")
    if which:
        places.append(os.path.dirname(os.path.realpath(which)))
    places += [os.path.join(os.path.dirname(TOOLS), "build"), os.path.join(HOME, ".local/bin")]
    for p in places:
        if p and os.access(os.path.join(p, name), os.X_OK):
            return os.path.join(p, name)
    return None


def pack_info(name, root=PACKS):
    """A pack's version, the day its last shard was written, and its passage count; None when not installed."""
    m = load_json(os.path.join(root, name, "manifest.json"))
    if not m:
        return None
    built = 0
    for s in m.get("shards", []):
        try:
            built = max(built, os.path.getmtime(os.path.join(root, name, s["file"])))
        except (OSError, KeyError):
            pass
    return {"version": str(m.get("version", "")), "built": day(built) if built else "",
            "passages": sum(int(s.get("passages", 0)) for s in m.get("shards", [])), "shards": m.get("shards", [])}


def source_note(folder):
    return load_json(os.path.join(DOWNLOADS, folder, "source.json"), {}) or {}


def local_sums(folder):
    """The checksums the download script verified, from the download folder or the copy kept in the state folder."""
    kept = os.path.join(STATE_DIR, folder + ".sha256")
    for path in (os.path.join(DOWNLOADS, folder, "SHA256SUMS"), kept):
        try:
            with open(path) as f:
                sums = {}
                for line in f:  # "sha  name"
                    parts = line.split()
                    if len(parts) == 2:
                        sums[parts[1]] = parts[0]
        except OSError:
            continue
        if sums:
            if path != kept:  # keep a copy so the downloads can go
                os.makedirs(STATE_DIR, exist_ok=True)
                with open(kept, "w") as f:
                    for name, sha in sums.items():
                        f.write(f"{sha}  {name}\n")
            return sums
    return {}


# ---------------------------------------------------------------- the check

def check_finewiki(subset, folder, pack, state):
    """subset: enwiki or simplewiki. The remote listing's checksums against the verified local ones."""
    r = {"source": pack, "folder": folder, "installed": "", "available": "", "newer": None, "note": "", "fetch": []}
    info = pack_info(pack)
    note = source_note(folder)
    if info:
        r["installed"] = info["version"] + (f", built {info['built']}" if info["built"] else "") + (f", fetched {note['fetched']}" if note.get("fetched") else "")
    else:
        r["installed"] = "not installed"
    text = curl(f"{HF}/api/datasets/HuggingFaceFW/finewiki/tree/main/data/{subset}")
    try:
        files = {x["path"].rsplit("/", 1)[-1]: ((x.get("lfs") or {}).get("oid", ""), int(x.get("size", 0)), x["path"]) for x in json.loads(text)}
    except (ValueError, TypeError, AttributeError, KeyError):
        files = {}
    if not files:
        r["available"] = "couldn't reach huggingface.co"
        return r
    meta = load_json_text(curl(f"{HF}/api/datasets/HuggingFaceFW/finewiki"))
    changed = (meta or {}).get("lastModified", "")[:10]
    total = sum(s for _, s, _ in files.values())
    have = local_sums(folder)
    if have:
        added = [n for n in files if n not in have]
        removed = [n for n in have if n not in files]
        differ = [n for n in files if n in have and files[n][0] and files[n][0] != have[n]]
        if not (added or removed or differ):
            r["available"] = f"{len(files)} files, {human(total)}, all with the checksums you have" + (f" (dataset last changed {changed})" if changed else "")
            r["newer"] = False
        else:
            parts = []
            if differ:
                parts.append(f"{len(differ)} changed")
            if added:
                parts.append(f"{len(added)} new")
            if removed:
                parts.append(f"{len(removed)} gone")
            r["available"] = f"{len(files)} files, {human(total)}: " + ", ".join(parts) + (f" (dataset last changed {changed})" if changed else "")
            r["newer"] = True
    else:
        r["available"] = f"{len(files)} files, {human(total)}" + (f", dataset last changed {changed}" if changed else "")
        r["newer"] = bool(info and info["built"] and changed and changed > info["built"])
        r["note"] = "no checksum list on this machine to compare file by file; judged by the dataset's last-changed date"
    # A refresh fetches the whole subset: the pack's shards follow the file order, so one changed file shifts nothing
    # only when every file is rebuilt together.
    r["fetch"] = [(f"{HF}/datasets/HuggingFaceFW/finewiki/resolve/main/{path}", os.path.join(DOWNLOADS, folder, n), size, sha)
                  for n, (sha, size, path) in sorted(files.items())]
    state[pack] = {"files": {n: s for n, (s, _, _) in files.items()}, "seen": day(time.time())}
    return r


def check_stackexchange(state):
    r = {"source": "stackexchange (23 sites)", "folder": "stackexchange", "installed": "", "available": "", "newer": None, "note": "", "fetch": []}
    packs = [pack_info(f"se-{s}") for s in SE_SITES] + [pack_info("se-askubuntu"), pack_info("se-superuser")]
    installed = [p for p in packs if p]
    note = source_note("stackexchange")
    fetched = note.get("fetched", "")
    if installed:
        built = max(p["built"] for p in installed)
        r["installed"] = f"{len(installed)} of 23 packs, built {built}" + (f", fetched {fetched}" if fetched else "")
    else:
        r["installed"] = "not installed"
    meta = load_json_text(curl("https://archive.org/metadata/stackexchange"))
    files = {f["name"]: f for f in ((meta or {}).get("files") or []) if f.get("name") in SE_FILES}
    if not files:
        r["available"] = "couldn't reach archive.org"
        return r
    newest = max(int(f.get("mtime", 0)) for f in files.values())
    total = sum(int(f.get("size", 0)) for f in files.values())
    remembered = (state.get("stackexchange") or {}).get("files", {})
    changed = []
    for name, f in files.items():
        local = os.path.join(DOWNLOADS, "stackexchange", name)
        size = int(f.get("size", 0))
        if os.path.exists(local) and os.path.getsize(local) != size:
            changed.append(name)
        elif name in remembered and remembered[name].get("md5") and remembered[name]["md5"] != f.get("md5"):
            changed.append(name)
        elif fetched and day(int(f.get("mtime", 0))) > fetched:
            changed.append(name)
    missing = [n for n in SE_FILES if n not in files]
    r["available"] = f"{len(files)} dumps, {human(total)}, newest dated {day(newest)}" + (f"; {len(changed)} newer than yours" if changed else "; none newer than yours") + (f"; {len(missing)} no longer on the archive" if missing else "")
    r["newer"] = bool(changed)
    r["fetch"] = [(f"https://archive.org/download/stackexchange/{n}", os.path.join(DOWNLOADS, "stackexchange", n), int(files[n].get("size", 0)), "") for n in changed]
    state["stackexchange"] = {"files": {n: {"md5": f.get("md5", ""), "size": int(f.get("size", 0)), "mtime": int(f.get("mtime", 0))} for n, f in files.items()}, "seen": day(time.time())}
    return r


def check_wiktionary(state):
    r = {"source": "wiktionary-en", "folder": "wiktionary", "installed": "", "available": "", "newer": None, "note": "", "fetch": []}
    info = pack_info("wiktionary-en")
    note = source_note("wiktionary")
    fetched = note.get("fetched", "")
    r["installed"] = (info["version"] + (f", built {info['built']}" if info["built"] else "") + (f", fetched {fetched}" if fetched else "")) if info else "not installed"
    headers = curl(WIKTIONARY_URL, head=True)
    modified, size = "", 0
    for line in headers.splitlines():
        k, _, v = line.partition(":")
        if k.lower() == "last-modified":
            try:
                modified = time.strftime("%Y-%m-%d", time.strptime(v.strip(), "%a, %d %b %Y %H:%M:%S %Z"))
            except ValueError:
                modified = v.strip()
        if k.lower() == "content-length":
            try:
                size = int(v.strip())
            except ValueError:
                pass
    if not modified and not size:
        r["available"] = "couldn't reach kaikki.org"
        return r
    local = os.path.join(DOWNLOADS, "wiktionary", "kaikki.org-dictionary-English.jsonl")
    local_size = os.path.getsize(local) if os.path.exists(local) else (state.get("wiktionary-en") or {}).get("size", 0)
    r["available"] = f"file dated {modified}, {human(size)}"
    r["newer"] = bool((fetched and modified > fetched) or (local_size and size and local_size != size) or (info and info["built"] and modified > info["built"]))
    r["fetch"] = [(WIKTIONARY_URL, local, size, "")]
    state["wiktionary-en"] = {"modified": modified, "size": size if r["newer"] is False else local_size or size, "seen": day(time.time())}
    return r


def check_wordnet(state):
    r = {"source": "wordnet-en", "folder": "wordnet", "installed": "", "available": "", "newer": None, "note": "", "fetch": []}
    info = pack_info("wordnet-en")
    r["installed"] = (f"{info['version']} edition" + (f", built {info['built']}" if info["built"] else "")) if info else "not installed"
    meta = load_json_text(curl("https://api.github.com/repos/globalwordnet/english-wordnet/releases/latest"))
    tag = (meta or {}).get("tag_name", "")
    if not tag:
        r["available"] = "couldn't reach github.com"
        return r
    edition = "".join(c for c in tag if c.isdigit())[:4]
    r["available"] = f"{tag}, published {((meta or {}).get('published_at') or '')[:10]}"
    r["newer"] = bool(info and edition and edition > "".join(c for c in info["version"] if c.isdigit())[:4])
    zip_name = f"english-wordnet-{edition}-json.zip"
    r["fetch"] = [(f"https://github.com/globalwordnet/english-wordnet/releases/download/{tag}/{zip_name}", os.path.join(DOWNLOADS, "wordnet", zip_name), 0, "")]
    state["wordnet-en"] = {"tag": tag, "seen": day(time.time())}
    return r


def check_poetry():
    info = pack_info("gutenberg-poetry")
    return {"source": "gutenberg-poetry", "folder": "gutenberg-poetry", "installed": (f"{info['version']}, built {info['built']}" if info else "not installed"),
            "available": "a fixed file (v001); it does not change", "newer": False, "note": "", "fetch": []}


def run_checks(state, quiet=False):
    bar = None if quiet else bar_start("unllm-update-check", "unllm: asking the sources for newer data", 5)
    rows = []
    for label, fn in (("Wikipedia", lambda: check_finewiki("enwiki", "finewiki-en", "wikipedia-en", state)),
                      ("Simple Wikipedia", lambda: check_finewiki("simplewiki", "finewiki-simple", "wikipedia-simple", state)),
                      ("StackExchange", lambda: check_stackexchange(state)),
                      ("Wiktionary", lambda: check_wiktionary(state)),
                      ("WordNet", lambda: check_wordnet(state))):
        rows.append(fn())
        if bar:
            bar.step(detail=label)
    rows.append(check_poetry())
    if bar:
        bar.finish()
    state["checked"] = day(time.time())
    return rows


def check(as_json=False):
    state = load_json(STATE, {}) or {}
    rows = run_checks(state)
    save_json(STATE, state)
    pruned = prune_expired()
    newer = [r for r in rows if r["newer"]]
    unreachable = [r for r in rows if r["newer"] is None]
    if as_json:
        print(json.dumps({"sources": [{k: v for k, v in r.items() if k != "fetch"} for r in rows], "newer": [r["source"] for r in newer],
                          "unreachable": [r["source"] for r in unreachable]}, indent=1))
    else:
        w = max(len(r["source"]) for r in rows)
        for r in rows:
            mark = "NEWER" if r["newer"] else ("?" if r["newer"] is None else "ok")
            print(f"{mark:5} {r['source']:{w}}  installed: {r['installed']}")
            print(f"{'':5} {'':{w}}  available: {r['available']}")
            if r["note"]:
                print(f"{'':5} {'':{w}}  ({r['note']})")
        if newer:
            print(f"\nNewer data exists for {len(newer)}: " + ", ".join(r["source"] for r in newer) + ". \"unllm update\" fetches and installs it.")
        else:
            print("\nEverything installed is current." if not unreachable else "\nNothing newer among the sources that answered.")
        if unreachable:
            print("Could not reach: " + ", ".join(r["source"] for r in unreachable) + ".")
        if pruned:
            print(pruned)
    return 2 if unreachable else (1 if newer else 0)


# ---------------------------------------------------------------- the update

def index_path():
    if os.environ.get("UNLLM_MEANING"):
        return os.environ["UNLLM_MEANING"]
    paths = sorted(os.path.join(VECTORS, f) for f in os.listdir(VECTORS) if f.endswith(".unlpq")) if os.path.isdir(VECTORS) else []
    return paths[0] if paths else os.path.join(VECTORS, "wiki-full.unlpq")


def shard_table(index, packs=PACKS):
    """What the index's audit says: [{no, pack, file, index, pack_count, short}] and whether any shard differs."""
    embed = tool("unllm_embed")
    if not embed:
        return None, "unllm_embed is not built (it lives beside the unllm program)"
    out = subprocess.run([embed, "audit", index, packs], capture_output=True, text=True)
    rows = []
    for line in out.stdout.splitlines():
        m = re.match(r"shard\s+(\d+)\s+(\S+)\s+(\S+)\s+index\s+(\d+)\s+pack\s+(-?\d+)\s+(\S+)", line)
        if m:
            rows.append({"no": int(m.group(1)), "pack": m.group(2), "file": m.group(3), "index": int(m.group(4)), "pack_count": int(m.group(5)),
                         "short": m.group(6) != "complete"})
    return rows, "" if rows else (out.stderr.strip() or "the audit printed no shards")


def packs_of(row):
    """The packs a source refreshes, each with its inputs for the ingest tool."""
    folder = os.path.join(DOWNLOADS, row["folder"])
    if row["source"] == "wikipedia-en":
        return [("wikipedia-en", [folder])]
    if row["source"] == "wikipedia-simple":
        return [("wikipedia-simple", [folder])]
    if row["source"].startswith("stackexchange"):
        changed = [os.path.basename(dest) for _, dest, _, _ in row["fetch"]]
        out = []
        for name in changed:
            site = name.split(".")[0]
            out.append((f"se-{site}", [os.path.join(folder, name)]))
        return out
    if row["source"] == "wiktionary-en":
        return [("wiktionary-en", [os.path.join(folder, "kaikki.org-dictionary-English.jsonl")])]
    if row["source"] == "wordnet-en":
        return [("wordnet-en", [dest for _, dest, _, _ in row["fetch"]])]
    return []


def fetch(items, dry):
    """Download each (url, dest, size, sha); curl resumes a partial file; a checksum, when known, is verified."""
    total = sum(s for _, _, s, _ in items)
    log(f"fetch: {len(items)} file(s), {human(total)}" + (" (dry run: nothing fetched)" if dry else ""))
    if dry:
        for url, dest, size, _ in items:
            log(f"  {human(size):>9}  {dest}")
        return True
    bar = bar_start("unllm-update-fetch", "unllm update: downloading", max(1, total // (1 << 20)))
    done_before = 0
    for url, dest, size, sha in items:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        stop = threading.Event()

        def watch():
            while not stop.wait(2):
                try:
                    have = os.path.getsize(dest + ".part")
                except OSError:
                    have = 0
                if bar:
                    bar.set((done_before + have) >> 20, detail=os.path.basename(dest))

        t = threading.Thread(target=watch, daemon=True)
        t.start()
        cmd = ["curl", "-4", "-fL", "--retry", "8", "--retry-delay", "10", "--retry-all-errors", "-C", "-", "-s", "-o", dest + ".part", url]
        rc = subprocess.run(cmd).returncode
        stop.set()
        t.join()
        if rc != 0 or not os.path.exists(dest + ".part"):
            log(f"  FAILED  {url}")
            if bar:
                bar.finish()
            return False
        if sha:
            got = subprocess.run(["sha256sum", dest + ".part"], capture_output=True, text=True).stdout.split()[0]
            if got != sha:
                log(f"  checksum mismatch, left as {dest}.bad: {url}")
                os.replace(dest + ".part", dest + ".bad")
                if bar:
                    bar.finish()
                return False
        os.replace(dest + ".part", dest)
        done_before += size
        log(f"  fetched {os.path.basename(dest)} ({human(os.path.getsize(dest))})")
    if bar:
        bar.finish()
    return True


def write_sidecar(row):
    """The download folder's note, as download_knowledge.sh writes it, dated today."""
    folder = os.path.join(DOWNLOADS, row["folder"])
    note = source_note(row["folder"])
    stamp = time.strftime("%Y-%m")
    version = note.get("version", stamp)
    if row["source"].startswith("wikipedia"):
        version = (version.split(".")[0] if "." in version else stamp) + ("." + version.split(".")[1] if "." in version else "")
    elif row["source"] in ("stackexchange (23 sites)", "wiktionary-en"):
        version = stamp
    elif row["source"] == "wordnet-en":
        version = "".join(c for c in row["available"] if c.isdigit())[:4] or version
    note.update({"version": version, "fetched": time.strftime("%Y-%m-%d")})
    note.setdefault("name", row["source"])
    os.makedirs(folder, exist_ok=True)
    save_json(os.path.join(folder, "source.json"), note)
    # The verified checksums, for the next check.
    if row["source"].startswith("wikipedia"):
        with open(os.path.join(folder, "SHA256SUMS"), "w") as f:
            for _, dest, _, sha in row["fetch"]:
                if sha:
                    f.write(f"{sha}  {os.path.basename(dest)}\n")


def python_for_ingest(inputs):
    """The ingest tool needs pyarrow for Wikipedia's parquet files: the system python when it has it, else a small
    environment under <data>/venv made on first need."""
    if not any(p.endswith(".parquet") or os.path.isdir(p) for p in inputs):
        return sys.executable
    if subprocess.run([sys.executable, "-c", "import pyarrow"], capture_output=True).returncode == 0:
        return sys.executable
    venv = os.path.join(DATA, "venv")
    py = os.path.join(venv, "bin", "python")
    if not os.path.exists(py):
        log("making a small Python environment for pyarrow (Wikipedia's file format)")
        subprocess.run([sys.executable, "-m", "venv", "--system-site-packages", venv], check=True)
        subprocess.run([py, "-m", "pip", "install", "-q", "pyarrow"], check=True)
    return py


def build_packs(plan, dry):
    """Each pack built under the staging root by the ingest tool; a pack already built there for this version is kept."""
    for name, inputs in plan:
        staged = pack_info(name, os.path.join(STAGING, "packs"))
        log(f"build {name} from {', '.join(os.path.basename(i) for i in inputs)}" + (" (dry run)" if dry else ""))
        if dry:
            continue
        env = dict(os.environ, UNLLM_DATA=STAGING, UNLLM_KNOWLEDGE=DOWNLOADS)
        py = python_for_ingest(inputs)
        rc = subprocess.run([py, os.path.join(TOOLS, "unllm_ingest.py"), "add", *inputs, "--name", name, "--force"], env=env).returncode
        if rc != 0 or not pack_info(name, os.path.join(STAGING, "packs")):
            log(f"  {name}: the build failed or installed nothing (see {STAGING}/ingest.log)")
            return False
        if staged:
            log(f"  {name}: rebuilt")
    return True


def embed_shards(index, jobs, dry, state):
    """jobs: [(pack, file, no, sqlite_path)] -> codes files. Progress by passages, the rate remembered for estimates."""
    embed = tool("unllm_embed")
    codes_dir = os.path.join(STAGING, "codes")
    os.makedirs(codes_dir, exist_ok=True)
    out = []
    total = 0
    counts = {}
    for pack, file, no, path in jobs:
        n = count_passages(path)
        counts[(pack, file)] = n
        total += max(0, n)
    rate = float(state.get("embed_rate", 0) or 0)
    log(f"embed: {len(jobs)} shard(s), {total:,} passages" + (f", about {hours(total / rate)} at the last measured {rate:.0f} a second" if rate else ", speed unknown until measured") +
        (" (dry run)" if dry else ""))
    for pack, file, no, path in jobs:
        log(f"  shard {no:3d}  {pack}/{file}  {counts[(pack, file)]:,} passages")
    if dry:
        return [(os.path.join(codes_dir, f"{pack}-{no}.codes"), no, pack, file) for pack, file, no, _ in jobs]
    bar = bar_start("unllm-update-embed", "unllm update: embedding passages", max(1, total))
    done_before = 0
    t0 = time.time()
    for pack, file, no, path in jobs:
        target = os.path.join(codes_dir, f"{pack}-{no}.codes")
        if os.path.exists(target) and os.path.getmtime(target) > os.path.getmtime(path):
            log(f"  shard {no}: codes already made, kept")
            done_before += counts[(pack, file)]
            out.append((target, no, pack, file))
            continue
        cmd = [embed, "encode", index, MODEL, path, str(no), target]
        if shutil.which("systemd-inhibit"):
            cmd = ["systemd-inhibit", "--what=sleep", "--why=unllm update is embedding passages", *cmd]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True)
        assert proc.stdout is not None
        for line in proc.stdout:
            m = re.match(r"progress (\d+) of (\d+)", line)
            if m and bar:
                bar.set(done_before + int(m.group(1)), detail=f"{pack} {file}: {int(m.group(1)):,} of {int(m.group(2)):,}")
            elif line.startswith("wrote") or line.startswith("embedding") or line.startswith("runtime"):
                log("  " + line.rstrip())
        if proc.wait() != 0:
            log(f"  shard {no}: embedding failed")
            if bar:
                bar.finish()
            return None
        done_before += counts[(pack, file)]
        out.append((target, no, pack, file))
    if bar:
        bar.finish()
    secs = time.time() - t0
    if secs > 30 and total:
        state["embed_rate"] = round(total / secs, 1)
    return out


def count_passages(path):
    try:
        import sqlite3
        db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
        n = db.execute("SELECT COUNT(*) FROM passages").fetchone()[0]
        db.close()
        return int(n)
    except Exception:
        return -1


def patch_index(index, codes, dry):
    embed = tool("unllm_embed")
    out = os.path.join(STAGING, "vectors", os.path.basename(index))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    log(f"patch: {len(codes)} shard(s) into a new copy of the index at {out}" + (" (dry run)" if dry else ""))
    if dry:
        return out
    cmd = [embed, "patch", index, out]
    for path, no, pack, file in codes:
        cmd += ["--add", path, str(no), pack, file]
    res = subprocess.run(cmd, capture_output=True, text=True)
    log("  " + (res.stdout.strip() or res.stderr.strip()))
    return out if res.returncode == 0 else None


def candidate_packs(plan):
    """A folder that looks like the live packs with the staged ones in place of what they replace."""
    cand = os.path.join(STAGING, "candidate")
    shutil.rmtree(cand, ignore_errors=True)
    os.makedirs(cand)
    staged = {name for name, _ in plan}
    for name in os.listdir(PACKS):
        if name in staged or name.endswith(".building") or name.endswith(".previous"):
            continue
        os.symlink(os.path.join(PACKS, name), os.path.join(cand, name))
    for name in staged:
        os.symlink(os.path.join(STAGING, "packs", name), os.path.join(cand, name))
    return cand


def score(packs, index, label, tsv, state):
    """The verification questions through the unllm program on the given packs and index: how many right."""
    unllm = tool("unllm")
    env = dict(os.environ, UNLLM_PACKS=packs, UNLLM_MEANING=index, UNLLM_NO_SERVER="1")
    out_dir = os.path.join(STATE_DIR, "eval")
    res = subprocess.run([sys.executable, os.path.join(TOOLS, "evaluate.py"), "tsv", tsv, "--label", label, "--out", out_dir, "--unllm", unllm or "unllm"],
                         env=env, capture_output=True, text=True)
    m = re.search(r"right (\d+)", res.stdout)
    if not m:
        log("  the scorer gave no total: " + (res.stderr.strip()[-300:] or res.stdout.strip()[-300:]))
        return None
    return int(m.group(1))


def verify(plan, new_index, dry, state):
    tsv = os.environ.get("UNLLM_VERIFY_TSV") or state.get("verify_tsv", "")
    if not tsv or not os.path.exists(tsv):
        log("verify: no verification questions file (set UNLLM_VERIFY_TSV, or \"verify_tsv\" in the state file); the swap goes ahead unverified")
        return True
    live = index_path()
    log(f"verify: the questions in {os.path.basename(tsv)}, live set against the candidate" + (" (dry run)" if dry else ""))
    if dry:
        return True
    key = f"{os.path.getmtime(live):.0f}:{os.path.getsize(live)}"
    base = (state.get("baseline") or {})
    if base.get("key") != key:
        n = score(PACKS, live, "update-live", tsv, state)
        if n is None:
            return False
        base = {"key": key, "right": n, "when": day(time.time())}
        state["baseline"] = base
        save_json(STATE, state)
    cand = candidate_packs(plan)
    n = score(cand, new_index, "update-candidate", tsv, state)
    if n is None:
        return False
    log(f"  live {base['right']} right, candidate {n} right")
    if n < base["right"]:
        log("  the candidate is worse: not installed; its files stay under staging for a look")
        return False
    state["baseline"] = {"key": "pending", "right": n, "when": day(time.time())}
    return True


def swap(plan, new_index, dry, state):
    """Old packs and index to <data>/previous, the new ones in their place, the resident unllm stopped."""
    log("swap: the new packs and index go live; the old set is kept under previous" + (" (dry run)" if dry else ""))
    if dry:
        return True
    if os.path.isdir(PREVIOUS):
        shutil.rmtree(PREVIOUS)
    os.makedirs(os.path.join(PREVIOUS, "packs"))
    for name, _ in plan:
        live = os.path.join(PACKS, name)
        if os.path.isdir(live):
            os.rename(live, os.path.join(PREVIOUS, "packs", name))
        os.rename(os.path.join(STAGING, "packs", name), live)
    live_index = index_path()
    if os.path.exists(live_index):
        os.rename(live_index, os.path.join(PREVIOUS, os.path.basename(live_index)))
    os.rename(new_index, os.path.join(VECTORS, os.path.basename(new_index)))
    save_json(os.path.join(PREVIOUS, "retired.json"), {"retired": time.time(), "packs": [n for n, _ in plan], "index": os.path.basename(live_index)})
    subprocess.run(["pkill", "-f", "unllm --serv[e]"], capture_output=True)  # the next question starts a server on the new set
    shutil.rmtree(os.path.join(STAGING, "candidate"), ignore_errors=True)
    shutil.rmtree(os.path.join(STAGING, "codes"), ignore_errors=True)
    if state.get("baseline", {}).get("key") == "pending":
        new_live = os.path.join(VECTORS, os.path.basename(new_index))
        state["baseline"]["key"] = f"{os.path.getmtime(new_live):.0f}:{os.path.getsize(new_live)}"
    return True


def prune_expired():
    """The previous set deleted once it is older than KEEP_DAYS; the text to say when it happened."""
    r = load_json(os.path.join(PREVIOUS, "retired.json"))
    if not r:
        return ""
    age = (time.time() - float(r.get("retired", 0))) / 86400
    if age >= KEEP_DAYS:
        shutil.rmtree(PREVIOUS, ignore_errors=True)
        return f"The previous set, retired {age:.0f} days ago, has been deleted."
    return ""


def prune():
    if not os.path.isdir(PREVIOUS):
        log("nothing to prune: no previous set is kept")
        return 0
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(PREVIOUS) for f in fs)
    shutil.rmtree(PREVIOUS, ignore_errors=True)
    log(f"the previous set is deleted ({human(size)} freed)")
    return 0


def rollback():
    r = load_json(os.path.join(PREVIOUS, "retired.json"))
    if not r:
        log("nothing to roll back to: no previous set is kept")
        return 1
    for name in r.get("packs", []):
        old = os.path.join(PREVIOUS, "packs", name)
        live = os.path.join(PACKS, name)
        if os.path.isdir(old):
            if os.path.isdir(live):
                shutil.rmtree(live)
            os.rename(old, live)
    old_index = os.path.join(PREVIOUS, r.get("index", ""))
    if r.get("index") and os.path.exists(old_index):
        live_index = os.path.join(VECTORS, r["index"])
        if os.path.exists(live_index):
            os.remove(live_index)
        os.rename(old_index, live_index)
    shutil.rmtree(PREVIOUS, ignore_errors=True)
    subprocess.run(["pkill", "-f", "unllm --serv[e]"], capture_output=True)
    state = load_json(STATE, {}) or {}
    state.pop("baseline", None)
    save_json(STATE, state)
    log(f"rolled back: {', '.join(r.get('packs', []))} and the index are the previous ones again")
    return 0


def run(argv):
    dry = "--dry-run" in argv
    repair = "--repair" in argv
    full = "--full" in argv
    only = [argv[i + 1] for i, a in enumerate(argv) if a == "--source" and i + 1 < len(argv)]
    state = load_json(STATE, {}) or {}
    pruned = prune_expired()
    if pruned:
        log(pruned)
    for name in ("unllm_embed", "unllm"):
        if not tool(name):
            log(f"{name} is not built or not on the PATH; the update needs it")
            return 1
    index = index_path()
    if not os.path.exists(index):
        log(f"no vector index at {index}; build one first (bench/export_index.py full)")
        return 1
    table, why = shard_table(index)
    if not table:
        log(f"cannot read the index's shard table: {why}")
        return 1
    free = shutil.disk_usage(DATA).free
    log(f"disk: {human(free)} free under {DATA}" + ("; below 80 GB, so the old set goes as soon as the new one passes" if free < LOW_DISK else ""))
    plan, jobs = [], []
    if repair:
        short = [s for s in table if s["short"] and s["pack_count"] >= 0]
        if not short:
            log("repair: every shard in the index agrees with its pack; nothing to do")
            return 0
        log("repair: " + ", ".join(f"shard {s['no']} ({s['pack']}: index {s['index']:,}, pack {s['pack_count']:,})" for s in short))
        jobs = [(s["pack"], s["file"], s["no"], os.path.join(PACKS, s["pack"], s["file"])) for s in short]
    else:
        rows = run_checks(state, quiet=True)
        save_json(STATE, state)
        newer = [r for r in rows if (r["newer"] or full) and r["fetch"] and (not only or r["source"] in only or r["folder"] in only)]
        if not newer:
            log("nothing is newer than what is installed" + (" among the sources named" if only else "") + "; use --full to rebuild everything anyway")
            return 0
        log("sources to refresh: " + ", ".join(r["source"] for r in newer))
        items = [it for r in newer for it in r["fetch"]]
        need = sum(s for _, _, s, _ in items) * 3.5  # the download, the pack, the index copy, with room
        if free < need and not dry:
            log(f"not enough disk: {human(free)} free, about {human(need)} needed")
            return 1
        if not fetch(items, dry):
            return 1
        if not dry:
            for r in newer:
                write_sidecar(r)
        for r in newer:
            plan += packs_of(r)
        if not build_packs(plan, dry):
            return 1
        # The shard numbers the index uses for these packs; a pack new to the index takes the next free number.
        by_pack = {}
        for s in table:
            by_pack.setdefault(s["pack"], []).append(s)
        next_no = max([s["no"] for s in table] + [100]) + 1
        for name, _ in plan:
            info = pack_info(name, os.path.join(STAGING, "packs")) if not dry else (pack_info(name) or {"shards": []})
            shards = info["shards"] if info else []
            known = {s["file"]: s["no"] for s in by_pack.get(name, [])}
            for i, s in enumerate(shards):
                if s["file"] in known:
                    no = known[s["file"]]
                elif name == "wikipedia-en":
                    no = i
                else:
                    no = next_no
                    next_no += 1
                staged_file = os.path.join(STAGING, "packs", name, s["file"])
                # In a dry run the pack is not built yet: the live pack's shard stands in for the count.
                jobs.append((name, s["file"], no, staged_file if os.path.exists(staged_file) else os.path.join(PACKS, name, s["file"])))
    codes = embed_shards(index, jobs, dry, state)
    save_json(STATE, state)
    if codes is None:
        return 1
    new_index = patch_index(index, codes, dry)
    if not new_index:
        return 1
    if not verify(plan, new_index, dry, state):
        save_json(STATE, state)
        return 1
    if not swap(plan, new_index, dry, state):
        return 1
    if not dry and free < LOW_DISK:
        prune()
    save_json(STATE, state)
    log("done" + (" (dry run: nothing was changed)" if dry else "; the old set is kept seven days under " + PREVIOUS + " (unllm update --prune deletes it sooner, --rollback puts it back)"))
    return 0


def main(argv):
    if len(argv) >= 2 and argv[1] == "check":
        return check(as_json="--json" in argv[2:])
    if len(argv) >= 2 and argv[1] == "run":
        return run(argv[2:])
    if len(argv) >= 2 and argv[1] == "prune":
        return prune()
    if len(argv) >= 2 and argv[1] == "rollback":
        return rollback()
    print((__doc__ or "").strip().splitlines()[0])
    print("  unllm_update.py check [--json] | run [--dry-run] [--full] [--repair] [--source NAME]... | prune | rollback")
    return 0 if len(argv) < 2 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
