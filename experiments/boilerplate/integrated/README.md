# Integrated C++ brick pilot

`run.sh REQUEST_FILE NEW_RUN_DIR ACCEPTANCE_RUNNER` executes the pipeline from a plain English request in a new directory. Big Pickle receives the request and the output-schema instructions in interpret.txt. The schema describes one source file and independently requested outputs. It gives the interpreter no candidate cards or brick IDs. The UDPipe adapter annotates the returned role text. The deterministic BGE picker proposes a card or abstains for each role. When roles remain unfilled, local Qwen3 8B receives those roles and the 43-card catalog and picks a card or abstains for each. The runner preserves each deterministic assignment and skips the Qwen call when all roles are filled. unfilled.json lists the roles eligible for a Qwen call, and selection.json records the merged assignments.

adapters.json is library metadata for the three executable converter cards. It declares which semantic input and output formats each supports and its emitter symbols. route.jq checks each selected card against Big Pickle's structured output. The emitter derives CLI slots in model output order, recursively fills smaller providers, and gathers pkg-config dependencies. A missing, incompatible, duplicate-output or unbuildable provider becomes an explicit gap. The harness compiles the source and executes independent behavior checks. After a compiler failure it attempts the B19 native Clang fix-it pass when applicable. A complete assembly passing the checks exits immediately. A remaining failure or gap triggers one focused Big Pickle patch, followed by a new build and the same tests. The experiment reports failure if that patch still fails. Each stage retains its inputs or outputs under the run directory. A clarification leaves the structured question in calls/interpret/response.json.

The supplied evaluator takes an executable and a fresh writable directory and returns JSON with a nonempty checks object whose entries each contain a boolean pass_. A failing assertion may return a nonzero status along with its report. An unavailable or malformed evaluator stops the run. For these two converter benchmarks convert-check.sh uses the frozen B16 fixtures and sandbox. CHECK_CASE selects the independent test suite only. A new task needs an appropriate independent evaluator.

## Run on this machine

Use the rebuilt B20 picker from the USB experiment or build experiments/boilerplate/picker with ONNX Runtime as described in its README. The machine needs OpenCode access to opencode/big-pickle, local Ollama qwen3:8b, BGE, UDPipe, C++17, jq, pkg-config, the converter libraries, and the B16 and B19 archives. Program execution in the supplied benchmark uses bubblewrap, Poppler utilities and Pillow.

```sh
export CORPUS_ROOT=/run/media/whisper/UNLLM_CORPUS/unllm
export PICKER="$CORPUS_ROOT/experiments/boilerplate-B20/replay-tools/brick-picker"
export UDPIPE_MODEL="$PWD/.state/n16-bridge-8zw8jB/tools/ewt.udpipe"
pilot="$PWD/experiments/boilerplate/integrated"
printf '%s\n' 'Convert this JPG into a PDF' > /tmp/brick-jpg-request.txt
CHECK_CASE=A "$pilot/run.sh" /tmp/brick-jpg-request.txt /tmp/brick-jpg-new "$pilot/convert-check.sh"
```

Use CHECK_CASE=B for the PDF extraction suite. Each live run calls the interpreter. Only runs with unfilled roles call Qwen. A successful run records the acceptance stage and whether stage 5 was used in result.json. The generated executable lives in project/converter and accepts actual file paths. The JPG converter expects an input JPG and a fresh output PDF. The extraction converter expects an input PDF, fresh output text file, and a fresh or empty picture directory. Generated includes live in the sibling bricks directory.
