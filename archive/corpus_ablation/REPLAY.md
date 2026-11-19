# Reproducing the paired trial

Use the upgrade checkout and mount the configured corpus USB. The experiment reads the D1 source snapshots and one C++ and one Python Parquet shard pinned in the acquisition directories. It writes separate experimental indexes. The production pack registry and Wikipedia vector index are independent.

The C++ indexer uses Tree-sitter, MD4C, libarchive and the repository's Arrow/Parquet SDK. Build the two pinned grammar libraries in a fresh tools directory. The checkout used for this trial has them under `.state/corpus-ablation-tools`.

```sh
libs=$(mktemp -d)
for spec in cpp:f41e1a044c8a84ea9fa8577fdd2eab92ec96de02 python:293fdc02038ee2bf0e2e206711b69c90ac0d413f; do
  lang=${spec%%:*}
  revision=${spec#*:}
  git init -q "$libs/$lang"
  git -C "$libs/$lang" fetch -q --depth 1 "https://github.com/tree-sitter/tree-sitter-$lang.git" "$revision"
  git -C "$libs/$lang" checkout -q --detach FETCH_HEAD
  cc -O2 -fPIC -I"$libs/$lang/src" -shared "$libs/$lang/src/parser.c" "$libs/$lang/src/scanner.c" -o "$libs/$lang.so"
done
cmake -S . -B build -DUNLLM_CORPUS_ABLATION=ON
cmake --build build --target corpus_ablation_index corpus_ablation_trial unllm_algorithm
```

Run indexing, prediction and scoring as distinct stages. Scoring opens the frozen test rows after all predictions have been written and hashed. Use a fresh output directory for each replay.

```sh
uuid=$(cat ~/.config/unllm-upgrade/corpus.uuid)
mount=$(findmnt -rn -S "$(readlink -e "/dev/disk/by-uuid/$uuid")" -o TARGET)
test "$(cat "$mount/.unllm-volume")" = "$uuid" || exit 1
sources="$mount/unllm/sources"
shards="$sources/stack-v2-6c8f6f2651a40fcbbfbc9d2561f334e06aae4800"
out=$(mktemp -d "$mount/unllm/experiments/paired-replay-XXXXXX")
build/native/corpus_ablation_index "$libs" "$sources/programming-pilot" "$shards"/cpp/train-00000-*.parquet "$shards"/python/train-00000-*.parquet "$out/levels"
archive/corpus_ablation/run.sh "$out" build/native
archive/corpus_ablation/score.sh "$out" build/native/unllm_algorithm /usr/bin/python3
archive/corpus_ablation/report.sh "$out" "$out"
```

## P01 Python dependency packaging

Build the opt-in native resolver with `cmake --build build --target corpus_dependency_closure`. The experimental Python-target frontend parses a source file and inspects compiled code objects for global reads. It sends module symbol facts to the C++ resolver. Command-line output includes the selected names and bundled source for the existing isolated execution checker.

```sh
python3 archive/corpus_ablation/python_dependency_facts.py path/to/module.py function_name definition_line
```

The bounded resolver follows up to 32 unique names, covering plain module-level functions and explicit imports from its standard-library allowlist. Decorated functions, ambiguous or missing definitions, wildcard imports and unsupported imports can prevent packaging. This is a standalone experiment for Python output. The normal synthesis CLI has its existing execution path.

## P03 C++ template declarations

The [protocol](template_acceptance/protocol.txt) freezes the six fresh task IDs and the comparison with P02. The [USB measurement directory](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/template-retest-RmpBZf) holds the frozen queries, rankings, scorer and case-level outputs. Run the following from the upgrade checkout with the corpus mounted. The replay reads the original experiment inputs and writes new outputs in a fresh directory.

```sh
trial=$(cat .state/template-retest.path)
replay=$(mktemp -d "$(dirname "$trial")/template-replay-XXXXXX")
sha256sum -c "$trial/freeze.sha256" || exit 1
for input in queries.json ranks.json records.jsonl ids.json modules.jsonl evaluate.py score.py; do
  ln -s "$trial/$input" "$replay/$input"
done
cmake --build build --target corpus_ablation_index corpus_dependency_closure
volume=$(dirname "$(dirname "$trial")")
build/native/corpus_ablation_index --selected-templates .state/corpus-ablation-tools \
  "$(cat .state/state-composition.path)/files.ids" "$(cat .state/focused-expansion.path)/sources" \
  "$replay/templates.json" "$volume"/sources/stack*/*/*.parquet
python3 "$replay/score.py" "$replay"
```

The extractor attaches a declaration only when the file digest, definition line and exact body hash match. The scorer saves and hashes winners before running reserved cases. Elapsed times vary across replays.
