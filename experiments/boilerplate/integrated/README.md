# Integrated C++ brick pilot

`run.sh REQUEST_FILE NEW_RUN_DIR ACCEPTANCE_RUNNER` takes an English request and writes its interpretation, selections, generated C++ and evaluator reports into a new directory. With the active provider bundle, the hosted interpreter returns one structured response covering the deliverable, output roles, reusable responsibilities, observable operations, parameters and outstanding requirements. A local SQLite FTS5 search and catalog type information limit the operation descriptions shown to the interpreter. The model never has to name a brick. A clarification is saved in `calls/interpret/response.json`.

For a CLI, the Arch lookup searches official Arch and AUR metadata when its catalog is present. Eligible whole programs go through the requested evaluator. For assembly, UDPipe annotates the output roles and the deterministic BGE picker searches the original 43 whole-output cards. When this picker abstains, a unique converter adapter can fill an output if its input type, output type, content and multiplicity match and there are no further structured requirements. The route records its adapter ID. The integrated runner then uses the component catalog and native planner for outstanding outputs. For a header, the runner selects components and emits the declared public interface. [Arch lookup and install choices](../arch/README.md)

adapters.json is library metadata for the three executable converter cards. It declares which semantic input and output formats each supports and its emitter symbols. route.jq checks each selected card against the interpreter's structured output. The emitter derives CLI slots in model output order, recursively fills smaller providers, and gathers pkg-config dependencies. A missing, incompatible, duplicate-output or unbuildable provider becomes an explicit gap. The harness compiles the source and executes independent behavior checks. After a compiler failure it attempts the B19 native Clang fix-it pass when applicable. A complete assembly passing the checks and covering its interpreted requirements exits immediately. A remaining failure or gap triggers one focused hosted edit, followed by a new build and the same tests. The experiment reports failure if that patch still fails. Each stage retains its inputs or outputs under the run directory.

For a missing function, the edit request supplies its generated name and the two path parameters. The response provides file-scope support code and body lines keyed by that name. The assembler renders the signatures, braces and complete gap header, validating that every expected name appears exactly once before applying any edits. Other files still support numbered range edits for changes such as passing an earlier result to a later operation. Malformed responses and rejected edits are recorded in `result.json`. Run `experiments/boilerplate/integrated/test-named-repair.sh` to check multiple gaps, mixed wiring changes, rejected patches and ordinary repairs.

The supplied evaluator takes the generated executable or header path and a fresh writable directory and returns JSON with a nonempty checks object whose entries each contain a boolean pass_. A failing assertion may return a nonzero status along with its report. An unavailable or malformed evaluator stops the run. For these two converter benchmarks convert-check.sh uses the frozen B16 fixtures and sandbox. CHECK_CASE selects the independent test suite only. For a header the evaluator compiles and runs a separate client against that path. A new task needs an appropriate independent evaluator.

## Emitting selected components

For older component catalogs without provider contracts, an open whole-output role triggers a second interpreter call for reusable responsibilities and a wiring proposal. The proposal can specify fields, comparisons, state updates, and an output operation. It has no access to candidate brick IDs or test fixtures. The picker then runs, its selected headers are placed, and the native component emitter checks each proposed operation against the selected implementations and the role's input and output formats. It registers complete providers with the existing emitter, which writes the CLI and build dependencies. The compiler and behavioral evaluator run next. A passing build skips the edit call. Each unsupported output retains its own gap for fallback. Whole-output roles with existing adapters keep their original wiring.

The current wiring vocabulary covers TSV traversal, conditionals, per-key latches, FIFO matching, JSON scalar arrays, and gzip decoding. The per-run `components/emission.json` records which selected bricks were bound to each output and why other roles stayed open. C++ compilation checks the assembled expressions and the acceptance suite checks their behavior. [Structured interpretation](wiring.txt) · [Operation bindings](wiring-operations.json)

## Planning from observable requirements

The normal runner enables native provider planning when the component bundle contains `providers/contracts.json`. Otherwise it uses the wiring-proposal path above. With a provider bundle, the interpreter returns the intended outcome, parameters, reusable responsibilities and any additional requirements for each output. The prompt exposes the outcome dictionary and the CLI's runtime guarantees. Provider IDs, C++ calls and their order come from local catalog metadata.

After component selection the planner searches all registered providers. It minimizes missing conversions, then sequence length, and uses picker preferences to break remaining cost ties. The existing symbolic planner checks typed connections in the resulting chain. A complete chain goes directly to compilation and the behavioral evaluator. A chain with missing conversions retains its implemented steps and declares the missing C++ signatures. When these are the only implementation gaps, the editor can supply their bodies and support code. The generated connector stays fixed. A CLI output can request a sequence of operations. The planner tracks the current port type and how many requested operations have been completed. Parameters belong to each occurrence, so a repeated operation can use different values. Adapters can connect steps without satisfying an unrelated operation. A missing transformation stays open. A header has a separate library contract describing its supported class, value types and methods. B30 includes a cancellable FIFO with unique membership.

The run directory records the picker's proposals in `components/picker-helpers.json` and the actual providers, proof, assumptions and source in `components/emission.json`. If the executable passes all supplied checks but the interpreter has outstanding English requirements, `result.json` reports `completed_at: "requires_verification"`, `checks_passed: true` and the outstanding items. The executable remains in `project/converter` and the runner returns status 2. A replay can supply a previously recorded object with `responsibilities` and `requirements` via `BRICK_REQUIREMENTS_FILE`. That override retains the earlier split replay interface. Normal live provider runs obtain both parts from one interpretation.

From the repository root, using the machine paths below, build the native planner and run its catalog controls with:

```sh
cmake --build build --target brick-provider-plan brick-header-emit
ctest --test-dir build -R '^brick_(provider|header)_composition$' --output-on-failure
python3 experiments/boilerplate/integrated/test-interpret-schema.py
```

The schema regression test uses Python and jsonschema. The B32 experiment log contains the earlier local interpreter measurements.

## Run on this machine

Each generative step tries `opencode/big-pickle`, then `opencode/iceman-free`. If both fail to return a JSON object, the runner tries each remaining hosted `opencode/*-free` ID once in the order reported by `opencode models`. If all OpenCode attempts fail and at least one reports a quota error or HTTP 429, the runner tries OVHcloud’s anonymous `gpt-oss-120b` endpoint, then AI Horde’s hosted `Qwen/Qwen3.8-Flash-Next` with its published anonymous credential. Each receives the stage prompt, including generated source excerpts during an edit. AI Horde dispatches that prompt to a volunteer worker. Its queue wait is bounded to six minutes, and the runner cancels an outstanding job on timeout. Each numbered directory under `calls/<stage>/attempts/` records the model, raw output and exit code. Independent provider attempts also retain the request payload, HTTP status and response body. A successful stage links its accepted JSON as `calls/<stage>/response.json`. The deterministic picker, BGE encoder, trained component classifier and C++ planner handle brick selection.

Use the archived B27 picker from the USB experiment or build experiments/boilerplate/picker with ONNX Runtime as described in its README. The machine needs OpenCode access to hosted free models, curl, BGE, UDPipe, C++17, jq, pkg-config, the converter libraries, and the B16 and B19 archives. Program execution in the supplied benchmark uses bubblewrap, Poppler utilities and Pillow.

```sh
export CORPUS_ROOT=/run/media/whisper/UNLLM_CORPUS/unllm
export PICKER="$CORPUS_ROOT/experiments/boilerplate-B27/replay-tools/brick-picker"
export UDPIPE_MODEL="$PWD/.state/n16-bridge-8zw8jB/tools/ewt.udpipe"
pilot="$PWD/experiments/boilerplate/integrated"
printf '%s\n' 'Convert this JPG into a PDF' > /tmp/brick-jpg-request.txt
CHECK_CASE=A "$pilot/run.sh" /tmp/brick-jpg-request.txt /tmp/brick-jpg-new "$pilot/convert-check.sh"
```

When `BRICK_COMPONENT_CATALOG` is unset the runner loads `$CORPUS_ROOT/boilerplate/active` if present. The active B32 USB bundle has 101 cards, including 58 behavior-checked components. Its immediate predecessor is the B30 100-card bundle. Set the variable to the absolute path of an archived bundle to replay it, or to `off` for a whole-output-only control. The native binaries default to `build/native`. `BRICK_PROVIDER_PLANNER`, `BRICK_LOGIC_WORKER` and `BRICK_HEADER_EMITTER` can override them. The older B27 wiring path also supports `COMPONENT_EMITTER`. [Building and verifying a bundle](../component-trial/README.md)

Use CHECK_CASE=B for the PDF extraction suite. Each live run calls the interpreter. A successful run records the acceptance stage and whether stage 5 was used in result.json. The generated executable lives in project/converter and accepts actual file paths. The JPG converter expects an input JPG and a fresh output PDF. The extraction converter expects an input PDF, fresh output text file, and a fresh or empty picture directory. Generated includes live in the sibling bricks directory.

## Creating a brick from a missing reader

Set `BRICK_LEARN_CLIENT` to an independent C++ test of the missing conversion and `BRICK_LEARN_DEST` to a new bundle directory on the corpus device. The test calls `UNLLM_TEST_BRICK(path)` and can create its own files in the sandbox. Set `BRICK_EDITOR=off` to measure what the pipeline can do without the model edit pass. Build `brick-gir-adapter` with the native CMake project and pugixml first.

The runner tries learning after a failed build or behavior check when it has one missing typed conversion. The pilot handles text-file input to a vector of strings. It searches installed GLib introspection metadata for a function that documents the exact MIME type and has a matching argument, result and ownership convention. It can generate a wrapper invoking the function on the whole file or on each line. The first variant that passes the requesting program's tests is rebuilt and checked with the independent client. A failed client ends the admission attempt.

The publisher writes a new bundle after its component tests pass. It records the C++ source, upstream symbol, GIR checksum, invocation mode, input/output types, independent client and qualification receipt. To use the published bundle on the next request, set `BRICK_COMPONENT_CATALOG` to the same path as `BRICK_LEARN_DEST` and unset the two learning variables. The trained classifier keeps its original label set. The native planner can bind the newly registered adapter immediately. [Two-request experiment](../stress/brick-learning/protocol.txt)
