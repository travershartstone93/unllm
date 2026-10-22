#!/usr/bin/env bash
# Download the reference sources unllm builds its knowledge packs from, into
# ~/Downloads/unllm-knowledge (one folder per source). curl resumes partial
# files, so re-running continues where it stopped. Each folder gets a
# source.json naming the source, version and license, which the pack builder
# reads. IPv4 is forced: some hosts have broken IPv6 routes.
#
# Wikipedia comes from FineWiki (HuggingFaceFW/finewiki): text extracted from
# Wikipedia's own rendered pages, so measurements, units and formulas survive
# (the older wikimedia/wikipedia dump drops every number written with a unit
# template). Each FineWiki file is checked against the sha256 Hugging Face
# publishes for it; a file that does not match is renamed *.bad and fetched
# again on the next run.
#
#   download_knowledge.sh                 download everything missing
#   download_knowledge.sh --sidecars-only write the source.json files only
export PATH=/usr/local/bin:/usr/bin:/bin
set -u
ROOT="${UNLLM_KNOWLEDGE:-$HOME/Downloads/unllm-knowledge}"
LOG="$ROOT/download.log"
FINEWIKI_VERSION=2025-08  # the snapshot month FineWiki was built from; raise it when FineWiki publishes a newer one
mkdir -p "$ROOT"/{finewiki-en,finewiki-simple,wiktionary,wordnet,gutenberg-poetry,stackexchange}

sidecar() {  # folder, name, source, version, url, license
    printf '{"name": "%s", "source": "%s", "version": "%s", "url": "%s", "license": "%s", "fetched": "%s"}\n' \
        "$2" "$3" "$4" "$5" "$6" "$(date +%F)" >"$1/source.json"
}

write_sidecars() {
    sidecar "$ROOT/finewiki-en" wikipedia-en HuggingFaceFW/finewiki "$FINEWIKI_VERSION.en" \
        https://huggingface.co/datasets/HuggingFaceFW/finewiki "CC BY-SA 4.0"
    sidecar "$ROOT/finewiki-simple" wikipedia-simple HuggingFaceFW/finewiki "$FINEWIKI_VERSION.simple" \
        https://huggingface.co/datasets/HuggingFaceFW/finewiki "CC BY-SA 4.0"
    sidecar "$ROOT/wiktionary" wiktionary-en kaikki.org "$(date +%Y-%m)" \
        https://kaikki.org/dictionary/English/ "CC BY-SA 4.0 and GFDL"
    sidecar "$ROOT/wordnet" wordnet-en globalwordnet/english-wordnet 2025 \
        https://github.com/globalwordnet/english-wordnet "CC BY 4.0"
    sidecar "$ROOT/gutenberg-poetry" gutenberg-poetry gutenberg-poetry-corpus v001 \
        https://github.com/aparrish/gutenberg-poetry-corpus "Public domain (US)"
    sidecar "$ROOT/stackexchange" stackexchange archive.org/stackexchange "$(date +%Y-%m)" \
        https://archive.org/details/stackexchange "CC BY-SA (2.5, 3.0 or 4.0 by post date)"
}

if [ "${1:-}" = "--sidecars-only" ]; then
    write_sidecars
    exit 0
fi

get() {  # url, destination file
    local url="$1" out="$2"
    if [ -s "$out" ] && [ ! -e "$out.part" ]; then echo "$(date +%T) have  $out" >>"$LOG"; return 0; fi
    echo "$(date +%T) start $url" >>"$LOG"
    touch "$out.part"
    if curl -4 -fL --retry 8 --retry-delay 10 --retry-all-errors -C - -s -o "$out" "$url"; then
        rm -f "$out.part"
        echo "$(date +%T) done  $out ($(du -h "$out" | cut -f1))" >>"$LOG"
    else
        echo "$(date +%T) FAIL  $url" >>"$LOG"
    fi
}
finewiki() {  # subset (enwiki, simplewiki), destination folder
    local sub="$1" dir="$2" path sha out
    : >"$dir/SHA256SUMS"
    curl -4 -s "https://huggingface.co/api/datasets/HuggingFaceFW/finewiki/tree/main/data/$sub" |
        python3 -c "import json,sys; [print(x['path'], (x.get('lfs') or {}).get('oid', '-')) for x in json.load(sys.stdin)]" |
        while read -r path sha; do
            out="$dir/$(basename "$path")"
            get "https://huggingface.co/datasets/HuggingFaceFW/finewiki/resolve/main/$path" "$out" </dev/null
            [ -s "$out" ] || continue
            if [ "$sha" != "-" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" != "$sha" ]; then
                echo "$(date +%T) FAIL  checksum $out (renamed .bad)" >>"$LOG"
                mv "$out" "$out.bad"
            else
                echo "$sha  $(basename "$out")" >>"$dir/SHA256SUMS"
            fi
        done
}
export LOG ROOT

echo "$(date) ==== download run starts" >>"$LOG"

get "https://github.com/globalwordnet/english-wordnet/releases/download/2025-edition/english-wordnet-2025-json.zip" "$ROOT/wordnet/english-wordnet-2025-json.zip"
get "http://static.decontextualize.com/gutenberg-poetry-v001.ndjson.gz" "$ROOT/gutenberg-poetry/gutenberg-poetry-v001.ndjson.gz"
get "https://www.gutenberg.org/cache/epub/feeds/pg_catalog.csv" "$ROOT/gutenberg-poetry/pg_catalog.csv"
finewiki simplewiki "$ROOT/finewiki-simple"

for site in cooking diy gardening travel outdoors bicycles pets money lifehacks health \
            english ell physics chemistry biology astronomy history mechanics \
            unix electronics math; do
    get "https://archive.org/download/stackexchange/$site.stackexchange.com.7z" "$ROOT/stackexchange/$site.stackexchange.com.7z"
done
for dom in askubuntu.com superuser.com; do
    get "https://archive.org/download/stackexchange/$dom.7z" "$ROOT/stackexchange/$dom.7z"
done

get "https://kaikki.org/dictionary/English/kaikki.org-dictionary-English.jsonl" "$ROOT/wiktionary/kaikki.org-dictionary-English.jsonl"

finewiki enwiki "$ROOT/finewiki-en"

write_sidecars
fails=$(grep -c FAIL "$LOG")
echo "$(date) ==== download run ends; failures logged: $fails" >>"$LOG"
