# C++ brick picker

The integrated runner uses `evidence-bge` and `resolve` for whole-output bricks. With the active component catalog it uses a trained local classifier to select helpers for remaining outputs. The Ollama commands below belong to earlier standalone comparisons. [Integrated runner](../integrated/README.md) · [Local setup and corpus paths](../../../docs/local-setup.md)

The picker accepts a shortlist of catalog cards and an array of atomic requirements. Each role gets exactly one existing brick ID or JSON null. The shared resolver deduplicates assigned bricks in role order and lists unfilled role IDs. A brick may serve more than one role. A model or input error exits nonzero so a caller can distinguish failure from an uncovered role.

A catalog card has a unique string `id`, a `description` and optional string `tags`. A role has a unique string `id`, its `text` and optional `requires`. Required tags must all appear on the chosen card. Tags are explicit compatibility constraints supplied by the caller. Each matcher uses the same resolver.

Build with CMake, a C++17 compiler, and pkg-config packages xapian-core, libcurl and json-c. The regression test and comparison harness also use jq and Bash. Example from the upgrade checkout using the saved B10 fixtures:

```sh
picker=experiments/boilerplate/picker
data=/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B10
cmake -S "$picker" -B /tmp/unllm-picker-build
cmake --build /tmp/unllm-picker-build
ctest --test-dir /tmp/unllm-picker-build --output-on-failure
bin=/tmp/unllm-picker-build/brick-picker
jq '.[0].roles' "$data/cases.json" > /tmp/brick-roles.json
"$bin" bm25 "$data/catalog.json" /tmp/brick-roles.json
"$bin" ollama "$data/catalog.json" /tmp/brick-roles.json qwen3:8b
```

BM25 builds an in-memory Xapian index from the card descriptions, stems English words and filters common function words. An eligible hit must match at least 40% of the distinct query terms. The picker abstains when the second eligible hit meeting this cutoff scores at least 85% of the first. These defaults were fixed before B10. The index is rebuilt on each CLI invocation.

The optional `bm25-capped` mode caps the required number of distinct matches at three. It uses the same compatibility and runner-up checks. To replay B11, pass the [enriched catalog](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B11/rich.json) to either lexical mode. Its ten cards append source wizard and template metadata, snippet text, or the verified catalog description to the B10 descriptions. Compare both factors independently with the [B11 scores](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B11/scores.json). The original catalog and mode remain available for comparison.

The experimental `reviewed-guard` mode takes a fourth argument containing audited examples. Use `brick-picker reviewed-guard CATALOG ROLES REVIEWS`. The reviews object contains the catalog snapshot and a nonempty `examples` array of objects with `text` and `brick` (a card ID or null). IDs, descriptions and tag sets must match the supplied catalog; ordering can differ. A historical null describes the available capabilities of that particular shortlist. For the comparison script, set `PICKER_REVIEWS` to that file.

The matcher starts with capped BM25 and compares each role against the examples by overlap of distinct English stems, giving an extra match to identical first content words. A neighbor needs two shared terms, a similarity of at least 0.50 and a lead of at least 0.10 over every other binding, including null. Otherwise a tentative BM25 assignment needs evidence for its first content word, either in the card itself or at the start of a previously fulfilled role. This is a first-word heuristic; it can reject legitimate unfamiliar phrasings. The `reviewed` ablation skips this final guard. In B12 the guard improved one fresh set but lost exact matches on the development check, so `bm25-capped` remains the reference. [Example reviews](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B12/test-memory.json) · [Measured tradeoffs](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B12/scores.json)

The optional `guarded-bge` mode keeps every brick assigned by `reviewed-guard`. For each empty role it uses the local BGE-small-en-v1.5 sentence encoder to compare the role with card descriptions and prior reviewed examples. A brick must meet the B14 cosine threshold of 0.85, satisfy all required tags, and beat every competing label including the label for a missing capability. Ties abstain. A new first-pass error can still place an incorrect brick. Output includes `first_pass` and the final `bindings`. The native CPU encoder and reference vectors load on the first empty role and persist for the lifetime of the process. A role array per line on standard input lets repeated requests share that cost. Build with the ONNX Runtime CMake package installed. Set `PICKER_BGE=OFF` in CMake for a lexical-only build. [B14 measurements](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B14/fallback-scores.json) · [Encoder model card](https://huggingface.co/BAAI/bge-small-en-v1.5)

```sh
data=/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B14
encoder=/run/media/whisper/UNLLM_CORPUS/unllm/models/bge-small-en-v1.5
jq -c '.[].roles' "$data/confirmation.json" |
    "$bin" guarded-bge "$data/rich.json" - "$data/test-memory.json" "$encoder"
```

For a single request, replace `-` with its role-array file. In `compare.sh` use `PICKER_REVIEWS` and `PICKER_BGE_DIR` with `guarded-bge`. That harness starts a process for each invocation.

### Expanded C++ library and explicit requirements

The [32-brick library](/run/media/whisper/UNLLM_CORPUS/unllm/boilerplate/picker-library-v2) includes source assets by ID, checksums, donor references and optional `outputs` statements on each card. The picker indexes these statements and embeds them separately. Reviewed examples carry a snapshot of the output statements, descriptions and tags. Catalog changes require checking prior labels against the new capability set.

`evidence-bge` adds a second acceptance condition for unfilled roles. Two distinct recorded texts for the same brick must each score at least 0.75, and its best match must lead the best competing label, including the no-brick label, by 0.05. The B14 rule at 0.85 is also available. The lexical first pass can still assign a wrong brick directly.

`brick-requirements` uses the native UDPipe parser with an English model to annotate one JSON role array per line. It combines caller-supplied requirements with explicit names from `requirement-terms.json`. Recognized output nouns must occupy a supported grammatical position. Ambiguous, negated or alternative mentions can leave the text unannotated. Both picker backends accept the resulting arrays. In CMake, point `UDPIPE_INCLUDE_DIR` at the directory containing the public `udpipe.h` and `UDPIPE_LIBRARY` at the built UDPipe library to enable this executable. Set `PICKER_UDPIPE_TEST_MODEL` to the English model path to run its regression checks.

```sh
lib=/run/media/whisper/UNLLM_CORPUS/unllm/boilerplate/picker-library-v2
bge=/run/media/whisper/UNLLM_CORPUS/unllm/models/bge-small-en-v1.5
picker_src=experiments/boilerplate/picker
build=/tmp/unllm-picker-build
# roles.json holds a single JSON array. UDPIPE_MODEL is the English .udpipe file.
jq -c . roles.json | "$build/brick-requirements" "$picker_src/requirement-terms.json" "$UDPIPE_MODEL" > parsed.json
"$build/brick-picker" evidence-bge "$lib/catalog.json" parsed.json "$lib/reviews.json" "$bge"
"$build/brick-picker" ollama "$lib/catalog.json" parsed.json qwen3:8b
```

Both the annotator and the deterministic picker also accept a stream of role arrays so model and catalog setup can be shared between requests. [B15 results and replay](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B15/replay.txt)

The Ollama backend sends one local request per role list with a per-role JSON Schema of allowed IDs and null. The caller supplies the model name. The resolver checks all returned keys, IDs and tags even after constrained generation. For offline replay use `brick-picker resolve CATALOG ROLES BINDINGS`, where BINDINGS is the saved role-to-ID object. Both matchers produce a placement proposal. Rendering a project or verifying that a brick delivers the behavior takes place downstream.

Two experimental modes run `reviewed-guard` first. Both return its assignments in `first_pass` and the final assignments in `bindings`. In `fill-gaps`, the model receives only the unfilled roles and the full catalog. The picker skips inference when all roles already have a brick. In `review-all`, the model receives every role, the full catalog and the provisional assignments, and can change any assignment, including removing a brick. The output includes model token counts whenever inference runs.

```sh
data=/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B13
jq '.[0].roles' "$data/new.json" > /tmp/brick-roles.json
"$bin" fill-gaps "$data/rich.json" /tmp/brick-roles.json "$data/test-memory.json" qwen3:8b
"$bin" review-all "$data/rich.json" /tmp/brick-roles.json "$data/test-memory.json" qwen3:8b
```

To repeat the 60-run comparison, supply a fresh output path. Times include process startup and any model loading:

```sh
"$picker/compare.sh" "$bin" "$data/catalog.json" "$data/cases.json" /tmp/picker-comparison qwen3:4b-instruct-2507-q4_K_M qwen3:8b
jq -s -f "$picker/score.jq" /tmp/picker-comparison/rows.jsonl
```


### Supervised component picking

`brick-picker trained CATALOG ROLES MODEL_JSON BGE_DIR` classifies each reusable responsibility against the eligible catalog entries and a no-brick class. Inference runs in C++ with ONNX Runtime. The B27 experiment supplies the offline training data, a saved classifier, a comparison with Qwen on gaps and a tested integration with the program editor. [Training and evaluation](../component-trial/README.md)
