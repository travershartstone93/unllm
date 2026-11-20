# Running the C++ coding pilot on the research machine

The repository contains the runner, planner, picker source and test protocols. The full brick bundle, archived emitter, replay tools, language models and converter fixtures live outside Git. A fresh clone can run the [core library quickstart](../README.md#quickstart-for-the-core-library). To run the integrated pilot on another machine, supply the external assets below and point the environment variables at their locations.

## Required assets

| Location on the research machine | Used for |
|---|---|
| `/run/media/whisper/UNLLM_CORPUS/unllm` | USB corpus root. The runner reads the active bundle and experiment archives from here. |
| `$CORPUS_ROOT/boilerplate/active` | Component cards, trained selection weights, provider contracts and reusable headers. |
| `$CORPUS_ROOT/experiments/boilerplate-B16` | Converter catalog, reviews, UDPipe annotator and terms, fixture files and independent evaluator. |
| `$CORPUS_ROOT/experiments/boilerplate-B19` | Base C++ bricks, library definitions, emitter and Clang fix-it executable. |
| `$CORPUS_ROOT/experiments/boilerplate-B27/replay-tools/brick-picker` | Native picker binary. It can also be built from the [picker source](../experiments/boilerplate/picker/README.md). |
| `$HOME/.local/share/unllm-models/bge-small-en-v1.5` | BGE encoder weights, tokenizer data and configuration for the picker. |
| `.state/n16-bridge-8zw8jB/tools/ewt.udpipe` under the checkout | English UDPipe model for annotating each output role. Set `UDPIPE_MODEL` to its actual path. |

Use the [native build guide](UPGRADE.md#reproduce-installation-and-checks) to configure the symbolic build on Linux. The pilot also uses OpenCode for hosted interpretation and optional editing, curl for fallback providers, Bash, jq, the SQLite FTS5 CLI, pkg-config, Clang and a C++17 compiler. The BGE picker needs ONNX Runtime. The converter fixtures use Bubblewrap, Poppler utilities and Python with Pillow. The JPG converter links Leptonica. The PDF text and picture converter uses Poppler C++/GLib, GDK, GdkPixbuf, GIO and Cairo.

## Run the JPG-to-PDF case

From the repository root with the corpus mounted and the native build configured, set these paths. A different mount point works with the same corpus layout.

```sh
export CORPUS_ROOT=/run/media/whisper/UNLLM_CORPUS/unllm
export PICKER="$CORPUS_ROOT/experiments/boilerplate-B27/replay-tools/brick-picker"
export UDPIPE_MODEL="$PWD/.state/n16-bridge-8zw8jB/tools/ewt.udpipe"
export BGE_DIR="$HOME/.local/share/unllm-models/bge-small-en-v1.5"
```

Check the required assets and build the native targets before making a hosted call.

```sh
(
  set -eu
  test -x "$PICKER"
  test -r "$UDPIPE_MODEL"
  test -r "$BGE_DIR/model.onnx"
  test -r "$CORPUS_ROOT/boilerplate/active/providers/contracts.json"
  test -x "$CORPUS_ROOT/experiments/boilerplate-B16/replay-tools/brick-requirements"
  test -r "$CORPUS_ROOT/experiments/boilerplate-B16/validate.py"
  test -x "$CORPUS_ROOT/experiments/boilerplate-B19/emit"
  test -x "$CORPUS_ROOT/experiments/boilerplate-B19/compiler-repair"
  cmake --build build --target brick-provider-plan brick-header-emit
  ctest --test-dir build -R '^brick_(provider|header)_composition$' --output-on-failure
)
```

The run directory must be new. This example creates a fresh parent directory, submits the English request to the configured interpreter and prints the pipeline result.

```sh
pilot="$PWD/experiments/boilerplate/integrated"
work=$(mktemp -d)
printf '%s\n' 'Convert this JPG into a PDF' > "$work/request.txt"
CHECK_CASE=A "$pilot/run.sh" "$work/request.txt" "$work/run" "$pilot/convert-check.sh"
cat "$work/run/result.json"
```

On acceptance, `$work/run/project/converter` takes an input JPG and a fresh destination PDF path. For PDF text and JPEG extraction, use that task’s English request and `CHECK_CASE=B`. The evaluator chooses the fixtures. `BRICK_EDITOR=off` measures assembly with editing disabled. [Runner stages and provider order](../experiments/boilerplate/integrated/README.md)

## Pilot evidence on disk

The [experiment log](EXPERIMENT_LOG.md) records results in Git. Some entries also link to raw artifacts kept on the research volume. Resolve the following paths relative to `CORPUS_ROOT`.

| Artifacts | Location |
|---|---|
| Focused converter repairs, deterministic assembly, combined picker runs | `experiments/boilerplate-B18/README.md`, `experiments/boilerplate-B19/README.md`, `experiments/boilerplate-B21/README.md` |
| Original survey inventories | `boilerplate-sources/survey-01/inventory.json`, `boilerplate-sources/survey-02/inventory.json` |
| Historical verified catalog and repository discovery queue | `boilerplate/catalog.json`, `boilerplate-sources/github-template-leads-01.json` |

The KDevelop survey used the host’s installed templates under `/usr/share/kdevappwizard/templates`.

The separate reference-pack benchmark kept its full question sets and answer transcripts under `~/Downloads/unllm-corpus-sources/bench/audit/`. The two runs summarized in the [pack evaluation](PACKS.md#reference-pack-evaluation) are `cli-corpus50b-before.jsonl` and `cli-corpus50b-after-resident.jsonl`, both scored against `corpus50b.tsv`.
