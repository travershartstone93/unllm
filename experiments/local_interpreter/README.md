# Optional local-model interpretation experiment

The completed comparison did **not justify adding a model to the normal path**.
See [the results and symbolic follow-up findings](../../docs/LOCAL_MODEL_EXPERIMENT.md).
This directory preserves the experiment for inspection and reproduction.

This is a separate experiment for **program-request interpretation**. The normal
unllm CLI does not import it, select a model, or use a model fallback. The original
checkout is separate. No model name is built into the implementation.

The model extracts a small JSON requirements document: text/metrics, a numeric
state, a flat record collection, a clock, a percentage relation, a unit conversion
and explicitly targeted actions. The adapter uses the existing typed requirements
builders. Prolog/Soufflé planning, cvc5 synthesis, source emission and executable
validation remain in the existing code. The model supplies no executable code,
mathematical formulas, proofs, expected results or compiler commands.

The deliberately bounded extraction representation has one text body, one scalar
counter, one list, one clock, one percentage relation and one conversion. This is
an experiment in reaching supported compositions through different language, not
an extension to arbitrary algorithms or factual question answering.

## Run

Use the upgrade checkout's pinned environment and an already running Ollama server.
The extra JSON Schema validator is installed into an isolated ignored directory:

```sh
uv pip install --python .venv/bin/python --target .deps/interpreter-experiment \
  -r experiments/local_interpreter/requirements.lock.txt
ollama list
```

Choose an installed model by passing `--model NAME` or setting
`UNLLM_INTERPRETER_MODEL`. There is no default model and no automatic download.
The base URL is configurable with `--base-url` or `UNLLM_INTERPRETER_URL`; its
default is the local server at http://127.0.0.1:11434.

```sh
# Set this to the name of a model you have installed:
export UNLLM_INTERPRETER_MODEL='your-installed-model'

# Interpret one request, without generating a program:
.venv/bin/python experiments/local_interpreter/run.py --mode model \
  --question 'Give me a counter starting at 8.5 whose button takes away 2.25.'

# Add --language python or --language cpp to construct and validate its program:
.venv/bin/python experiments/local_interpreter/run.py --mode model --language python \
  --question 'Create a multiline text box with a word count and a trim button.'

# Compare with grammar interpretation and exercise selected outputs independently:
.venv/bin/python experiments/local_interpreter/run.py --execute

# Evaluate the existing grammar without a model:
.venv/bin/python experiments/local_interpreter/run.py --mode grammar

.venv/bin/python experiments/local_interpreter/tests/test_contract.py
```

Controls include `--timeout`, `--context`, `--max-tokens`, `--thinking off|on|default`,
`--suite` and `--output`. Thinking defaults off; a model that cannot accept that
option can use `--thinking default`. Sampling is fixed to temperature zero and
seed one for the comparison. The interpreter limits input length, output tokens,
response size and HTTP time. By default a used model remains loaded for ten
minutes through Ollama's keep_alive option. Reports include its installed manifest
digest and the selected configuration, so swapping model names does not alter
the pipeline or the benchmark inputs.

## Evaluation

The frozen cases.json contains 32 hand-annotated development requests: 24 supported
contracts and eight requiring refusal or clarification. Six are the original trial
requests; two retain the known coordination failure and its reordered control.
The rest exercise fresh phrasing, fractional operands/initial values, multiple
actions, unit direction, negation, missing requirements and unavailable features.
This is not a blinded or representative natural-language accuracy benchmark.

The prompt describes available capabilities and the output schema. Neither
interpreter receives the expected contract, workflow or reference outputs. The
same fixed prompt is used for every case, with no repair retries. Schema checking
rejects unknown fields and ill-typed extraction. The typed adapter rejects
unbound targets, incompatible units, repeated action targets, unsupported goals
and invented source quotations. None of these checks proves faithful intent.

The independent annotation checks all exposed state, text metrics, visible
actions, numeric initial values, scalar sample outcomes, conversion direction
and percentage behavior. Scalar observations use the existing cvc5 solver; their
results are compared with separately annotated arithmetic outcomes. Added or
missing requirements count as wrong acceptances. Refusal cases count as correct
only for explicit unsupported/ambiguous outcomes; transport errors and timeouts
count as failures. Refusal subtype/reason correctness is not measured by that score.

The grammar baseline uses a resident Engine and its ordinary bounded ACE/ERG
interpretation path, in fixture order. Model calls use separate two-message
requests to a resident local server. Their raw outputs, parse diagnoses, wall
times, model loading/token timings, prompt/source hashes and field mismatches are
preserved. Interpretation timing excludes grading, compilation and GUI checks.

With --execute, selected correctly interpreted specs are sent through the same
symbolic constructor in each requested language. Separate Qt harnesses drive
keyboard/mouse actions and check original-request outcomes and actual files.
Semantically incorrect candidates are recorded as failed/skipped against the
whole selection denominator, rather than being dropped from the result.

The benchmark command exits successfully when the measurement finishes, even if
interpretations are wrong. Read the reported counts; completion is not a quality
gate. Protocol tests exercise a controlled local HTTP endpoint and are separate
from model-quality measurements.

Reports are written to .benchmarks/local_interpreter_*.json by default, with jobs
under .artifacts and test state under .state/local-interpreter. Those directories
are ignored. A single-question command can produce a wrong but schema-valid spec;
inspect its interpreted requirements and do not equate program validation with
correct interpretation.

The HTTP schema/format and timing options follow the official
[Ollama chat API](https://docs.ollama.com/api/chat) and
[structured output documentation](https://docs.ollama.com/capabilities/structured-outputs).
