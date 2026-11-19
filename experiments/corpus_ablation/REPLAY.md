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
experiments/corpus_ablation/run.sh "$out" build/native
experiments/corpus_ablation/score.sh "$out" build/native/unllm_algorithm /usr/bin/python3
experiments/corpus_ablation/report.sh "$out" "$out"
```
