# B27 trained component picker

This experiment asks whether useful C++ bricks will be selected and executed on the same six B26 requests. Each request first goes through the existing interpreter, Arch/AUR lookup, whole-output picker, assembly and compiler checks. In the recorded B27 experiment, after the initial build needed repair, the optional component picker assigned a card or abstains for each reusable responsibility. The editor received the sources of its selected helpers. B28 moves component selection ahead of emission and attempts verified wiring before the edit stage. The unit of selection is a responsibility, so multiple assignments may put just one header in a program. [Frozen protocol](protocol.txt) · [Acceptance suite](../stress/cross-domain/README.md)

## B27 catalog and model

The expanded bundle retains all 43 original cards and adds seven reusable implementations. They cover TSV iteration, state transitions per identifier, FIFO pairing across two streams, dependency scheduling in waves, gzip member decoding, a cancellable unique queue and JSON array output. Their inputs, outputs, dependency names and source locations are in the [component manifest](components.json). Six sources were added here. The latch was developed during B26.

The native picker embeds each responsibility using the existing 384-dimensional BGE encoder. A supervised linear classifier scores each eligible brick and an explicit no-brick class. The selector abstains if no brick wins or if the best and second-best scores are too close. Training fits the classifier weights and biases with scikit-learn. The encoder stays frozen. Examples come from card descriptions, advertised outputs, existing reviewed picks and [short labeled responsibilities](examples.tsv). [Development examples](validation.tsv) select the abstention margin. The expanded fit has 219 distinct training texts and 51 classes, achieves 14/17 on development, and occupies 391,212 bytes in JSON. Adding brick IDs requires refitting. Refit after changing the meaning of existing cards as well.

## Running the experiment

Use the integrated pilot's existing B16/B19 archives, installed model and normal environment. Training needs NumPy and scikit-learn. Inference uses the native C++ picker with ONNX Runtime. From the repository root, the following builds a fresh bundle and uses it on one role array.

```sh
trial=experiments/boilerplate/component-trial
export CORPUS_ROOT=/run/media/whisper/UNLLM_CORPUS/unllm
export BGE_DIR="$HOME/.local/share/unllm-models/bge-small-en-v1.5"
export PICKER="$CORPUS_ROOT/experiments/boilerplate-B27/replay-tools/brick-picker"
export EMBEDDER="$PWD/build/unllm_meaning"
bundle=$(mktemp -d)/bundle
"$trial/build-bundle.sh" "$CORPUS_ROOT/experiments/boilerplate-B16" "$bundle" 50
roles="$CORPUS_ROOT/experiments/boilerplate-B26/candidate/decomposition/inventory.annotated.json"
"$PICKER" trained "$bundle/catalog.json" "$roles" "$bundle/model.json" "$BGE_DIR"
```

For the current component-first emission pipeline, build the picker CMake project (including `brick-component-emit`) or set `COMPONENT_EMITTER` to the archived B28 binary. To use it on a new English request, set `BRICK_COMPONENT_CATALOG` to the absolute bundle path and use the [integrated runner](../integrated/README.md). `BRICK_COMPONENT_MODE=trained` uses the classifier. `current` uses the deterministic evidence picker and records its abstentions. Historical B27 results for that arm include a Qwen fallback. The B30 interpreter sees shortlisted outcome descriptions and supplies responsibilities in its initial response. Set `BRICK_COMPONENT_ROLES` to an absolute saved role-array path for controlled replays. The original whole-output picker runs before this optional layer.

The B27 driver calls the integrated runner from the working checkout, so reruns reflect its current architecture. The archived driver replays a B26 model answer only when its prompt matches byte for byte. Changed prompts get fresh answers. For example, this runs one case into a new output directory and rescans the complete archived trial.

```sh
archive="$CORPUS_ROOT/experiments/boilerplate-B27"
"$archive/run-one.sh" expanded trained inventory "$(mktemp -d)/inventory"
python3 "$trial/summarize.py" "$archive"
```

The other driver arguments are `old|expanded`, `current|trained` and one of `inventory sensors finance build gzip queue`. The USB archive contains the four complete runs, model weights, training logs, compiler diagnostics, editor token records and mutation reports. To reproduce a mutation audit, invoke `audit-use.py RUN FROZEN_EVALUATOR NEW_OUTPUT_DIR` on a successful run with staged components. Both models reproduced byte for byte from their training inputs on the test host.

## Reading the scores

The [primary labels](gold.json) specify 13 positive responsibilities and seven for which the picker should abstain. These labels are fixed across catalogs. Separate nearest-neighbor controls mask unavailable labels to measure selection given available cards. The header request stops upstream of the picker in the primary trial. Its separate header diagnostic uses the same independent client. The training author knew the task domains. An unseen request set is needed to measure transfer. [Experiment log](../../../docs/EXPERIMENT_LOG.md#b27-trained-component-picker-on-the-frozen-cross-domain-requests) · [Per-case results](/run/media/whisper/UNLLM_CORPUS/unllm/experiments/boilerplate-B27/results.json)

## B30 provider bundles

`build-bundle.sh B16_DIRECTORY NEW_DIRECTORY [50|75|100]` defaults to 100. The 50-card control retains 43 whole-output cards and seven tested components. The larger sizes add 25 or 50 C++ component bindings. The full expansion contains 8 structured-data, 12 text, 10 filesystem, 10 SQLite and 10 archive/compression operations. The bindings and metadata are written for this project and call implementations shipped with Qt, pugixml, libstdc++, SQLite and libarchive. The manifest records the supporting API. [Operation manifest](expanded/operations.tsv)

The bundle builder stages a new directory, validates declared components, builds and exercises emitted C++, records source and metadata hashes and publishes the completed bundle. The expanded bundles retain the 50-card classifier labels. The native planner can find all registered providers by their declared inputs, outputs and effects. B30 compared the same requests under ordinary, perfect and empty picker hints. [Recorded results](../../../docs/EXPERIMENT_LOG.md#b30-verified-c-brick-library-and-staged-pipeline-upgrades)

To qualify code from a repaired CLI for catalog review, run `verify-repair.sh RUN BEHAVIOR_EVALUATOR HEADER_NAME INDEPENDENT_TEST.cpp NEW_RECEIPT`. It requires an accepted run, rebuilds the CLI, repeats its task checks, and builds and runs an independent client of the header in isolation. A passing run produces a receipt with the source and client digests. Catalog admission and classifier training remain separate decisions.
