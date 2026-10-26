#!/usr/bin/env python3
"""Keep the knowledge packs current.

  unllm_update.py check        ask each source whether newer data exists than what is installed; no download
  unllm_update.py check --json the same as one JSON object (for scripts)

The check asks each source's server for the current version of its files (a few small requests, never the files
themselves) and compares that with what the packs were built from:

  Wikipedia (FineWiki on Hugging Face)  the published checksum of every data file, against the checksums the download
                                        script verified (kept in the download folder, and copied into the state folder
                                        here so the downloads can be deleted afterwards)
  StackExchange (archive.org)           each site dump's date, size and checksum on the archive
  Wiktionary (kaikki.org)               the file's date and size on the server
  WordNet (GitHub)                      the latest release's edition
  Gutenberg poetry                      a fixed file; never changes

What is installed is read from the packs' own manifests (~/.local/share/unllm/packs/*/manifest.json) and, when still
present, the download folder's source notes (~/Downloads/unllm-knowledge/*/source.json). Remote fingerprints are
remembered in ~/.local/share/unllm/update/state.json, so a later check can say "unchanged since you last looked" even
after the downloads are gone. Exit code 0: everything is current; 1: something newer exists; 2: a source could not be
reached (its line says so; the rest are still checked).

All requests go through curl over IPv4 (huggingface.co over IPv6 does not answer from here).
"""
import json
import os
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
PACKS = os.environ.get("UNLLM_PACKS") or os.path.join(HOME, ".local/share/unllm/packs")
DOWNLOADS = os.environ.get("UNLLM_KNOWLEDGE") or os.path.join(HOME, "Downloads/unllm-knowledge")
STATE_DIR = os.path.join(HOME, ".local/share/unllm/update")
STATE = os.path.join(STATE_DIR, "state.json")

SE_SITES = ["cooking", "diy", "gardening", "travel", "outdoors", "bicycles", "pets", "money", "lifehacks", "health",
            "english", "ell", "physics", "chemistry", "biology", "astronomy", "history", "mechanics", "unix",
            "electronics", "math"]
SE_FILES = [f"{s}.stackexchange.com.7z" for s in SE_SITES] + ["askubuntu.com.7z", "superuser.com.7z"]
WIKTIONARY_URL = "https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl"


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


def day(epoch):
    return time.strftime("%Y-%m-%d", time.localtime(epoch))


def pack_info(name):
    """A pack's version and the day its first shard was written, from its manifest; None when not installed."""
    m = load_json(os.path.join(PACKS, name, "manifest.json"))
    if not m:
        return None
    built = 0
    for s in m.get("shards", []):
        try:
            built = max(built, os.path.getmtime(os.path.join(PACKS, name, s["file"])))
        except (OSError, KeyError):
            pass
    return {"version": str(m.get("version", "")), "built": day(built) if built else "", "passages": sum(int(s.get("passages", 0)) for s in m.get("shards", []))}


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


def check_finewiki(subset, folder, pack, state):
    """subset: enwiki or simplewiki. The remote listing's checksums against the verified local ones."""
    r = {"source": pack, "installed": "", "available": "", "newer": None, "note": ""}
    info = pack_info(pack)
    note = source_note(folder)
    if info:
        r["installed"] = info["version"] + (f", built {info['built']}" if info["built"] else "") + (f", fetched {note['fetched']}" if note.get("fetched") else "")
    else:
        r["installed"] = "not installed"
    text = curl(f"https://huggingface.co/api/datasets/HuggingFaceFW/finewiki/tree/main/data/{subset}")
    try:
        files = {x["path"].rsplit("/", 1)[-1]: ((x.get("lfs") or {}).get("oid", ""), int(x.get("size", 0))) for x in json.loads(text)}
    except (ValueError, TypeError, AttributeError):
        files = {}
    if not files:
        r["available"] = "couldn't reach huggingface.co"
        return r
    meta = load_json_text(curl("https://huggingface.co/api/datasets/HuggingFaceFW/finewiki"))
    changed = (meta or {}).get("lastModified", "")[:10]
    total = sum(s for _, s in files.values())
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
    state[pack] = {"files": {n: s for n, (s, _) in files.items()}, "seen": day(time.time())}
    return r


def load_json_text(text):
    try:
        return json.loads(text)
    except (ValueError, TypeError):
        return None


def check_stackexchange(state):
    r = {"source": "stackexchange (23 sites)", "installed": "", "available": "", "newer": None, "note": ""}
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
    # Newer when a dump on the archive is dated after the fetch, or its size differs from the file fetched (while the
    # downloads are still here) or from the size seen at the last check.
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
    state["stackexchange"] = {"files": {n: {"md5": f.get("md5", ""), "size": int(f.get("size", 0)), "mtime": int(f.get("mtime", 0))} for n, f in files.items()}, "seen": day(time.time())}
    return r


def check_wiktionary(state):
    r = {"source": "wiktionary-en", "installed": "", "available": "", "newer": None, "note": ""}
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
    state["wiktionary-en"] = {"modified": modified, "size": size if r["newer"] is False else local_size or size, "seen": day(time.time())}
    return r


def check_wordnet(state):
    r = {"source": "wordnet-en", "installed": "", "available": "", "newer": None, "note": ""}
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
    state["wordnet-en"] = {"tag": tag, "seen": day(time.time())}
    return r


def check_poetry():
    info = pack_info("gutenberg-poetry")
    return {"source": "gutenberg-poetry", "installed": (f"{info['version']}, built {info['built']}" if info else "not installed"),
            "available": "a fixed file (v001); it does not change", "newer": False, "note": ""}


def check(as_json=False):
    sys.path.insert(0, os.path.join(HOME, ".local/lib/taskprogress"))
    try:
        from taskprogress import Task
        bar = Task("unllm-update-check", "unllm: asking the sources for newer data", 5)
    except Exception:  # the helper is this machine's; the check works without it
        bar = None
    state = load_json(STATE, {}) or {}
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
    save_json(STATE, state)
    newer = [r for r in rows if r["newer"]]
    unreachable = [r for r in rows if r["newer"] is None]
    if as_json:
        print(json.dumps({"sources": rows, "newer": [r["source"] for r in newer], "unreachable": [r["source"] for r in unreachable]}, indent=1))
    else:
        w = max(len(r["source"]) for r in rows)
        for r in rows:
            mark = "NEWER" if r["newer"] else ("?" if r["newer"] is None else "ok")
            print(f"{mark:5} {r['source']:{w}}  installed: {r['installed']}")
            print(f"{'':5} {'':{w}}  available: {r['available']}")
            if r["note"]:
                print(f"{'':5} {'':{w}}  ({r['note']})")
        if newer:
            print(f"\nNewer data exists for {len(newer)}: " + ", ".join(r["source"] for r in newer) + ". An update is not built yet; the packs stay as they are.")
        else:
            print("\nEverything installed is current." if not unreachable else "\nNothing newer among the sources that answered.")
        if unreachable:
            print(f"Could not reach: " + ", ".join(r["source"] for r in unreachable) + ".")
    return 2 if unreachable else (1 if newer else 0)


def main(argv):
    if len(argv) >= 2 and argv[1] == "check":
        return check(as_json="--json" in argv[2:])
    print((__doc__ or "").strip().splitlines()[0])
    print("  unllm_update.py check [--json]")
    return 0 if len(argv) < 2 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
