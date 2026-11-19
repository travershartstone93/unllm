# Local interpretation experiment: results and decision

Completed on 2026-11-12. Keep the model out of the normal unllm path. The tested
model interpreters were slower and less reliable overall than the grammar.
The isolated [experiment](../archive/local_interpreter/README.md) remains
configurable and reproducible; no model name or model call was added to the normal
CLI. All 57 captured core source/executable hashes remain unchanged.

## Frozen comparison

Both models received the same fixed prompt and JSON requirements schema, with no
repair retries or prompt changes during evaluation. Neither received the expected
contracts or GUI workflows. There were 32 development cases: 24 supported requests
and eight requiring refusal or clarification. Six were the original manual-trial
requests, two were the known coordination example and its reordered control, and
16 were fresh supported phrasings. This is not a blinded accuracy estimate.

| Interpreter | Correct overall / 32 | Correct supported / 24 | Correct refusals / 8 | Wrong acceptances | Median interpretation |
| --- | ---: | ---: | ---: | ---: | ---: |
| Resident ACE/ERG and existing grounding | 20 | 12 | 8 | 0 | 0.763 s |
| Installed 4B model | 14 | 8 | 6 | 10 | 16.233 s |
| Installed 8B model | 18 | 12 | 6 | 8 | 5.165 s |

The selected runtime models were qwen3:4b-instruct-2507-q4_K_M and
odysseus-stable2:8b. These names were command-line arguments, not defaults or
branches in the implementation. Reports preserve Ollama manifest digests,
configuration and server version. Both used temperature zero, seed one, thinking
off, an 8,192-token context and a 1,400-token output limit. The 4B HTTP deadline
was 60 seconds and the 8B deadline 120 seconds; neither had a transport error.

Interpretation timing excludes semantic grading, compilation and GUI checks.
The grammar was resident and processed requests in fixture order. The model
requests used independent two-message inputs and a resident local server. Loading
and token timings are recorded separately. These timings concern these exact
configurations and machine; model parameter count alone did not predict speed.

## What improved, and what failed

On the 16 fresh supported phrasings, the grammar got four correct, the 4B model
five, and the 8B model eight. Model interpretation therefore did reach some
existing capabilities through language the grammar rejected. The larger model
correctly interpreted fractional initial values, two separate counter actions,
list controls and task checkboxes.

That extra coverage came with incorrect accepted meanings. Both models guessed a
save target for a request containing text and a list, and a reset target for a
request containing a timer and a counter. Other failures included unrequested
numeric controls, missing actions, reversed numerical changes and false refusals
of supported conversion/percentage requests. Correct refusal scores measure the
decision to stop; they do not establish that the explanation or refusal subtype
was right.

A post-hoc grammar-first simulation retained accepted or ambiguous grammar results
and consulted the recorded model prediction only when grammar returned unsupported.
It scored 23/32 with the 4B model and 26/32 with the 8B model. Each still had three
wrong acceptances. This is a simulation using stored predictions, not a deployed
or separately timed route. Its coverage gain does not justify silent fallback.

## Executable evidence

Nine request/language outputs were selected before evaluation for further testing.
They included the mixed text/counter tool, fractional initial state, list
save/load, percentages and conversion. Semantically incorrect interpretations
were recorded as failed/skipped against the full selection denominator.

| Interpreter | Selected outputs | Correctly interpreted, built and independently passed | Skipped for incorrect interpretation |
| --- | ---: | ---: | ---: |
| Grammar | 9 | 3 | 6 |
| 4B model | 9 | 0 | 9 |
| 8B model | 9 | 5 | 4 |

The five 8B outputs passed external Qt keyboard/mouse/file workflows: the mixed
tool in Python and C++, the fractional counter in both languages, and the list
save/load tool in Python. They also passed ordinary validation, including normal
and sanitizer checks for C++. Every built output in this selection passed its
independent workflow; the missing completions were interpretation failures.

Two diagnostic demonstrations were chosen after inspecting predictions and are
not included as additional benchmark completions. The smaller model's correctly
interpreted two-action counter generated working Python and C++ apps. Its
incorrectly interpreted decrement request also generated a Python app that passed
internal validation, but failed the independent original-request checks:

| Typed input | Required after decreasing by 11 | Generated app result |
| --- | ---: | ---: |
| 17.5 | 6.5 | 28.5 |
| 0.25 | -10.75 | 11.25 |
| -100 | -111 | -89 |

The model supplied operation subtract and amount -11. The backend correctly
implemented that wrong specification. Schema checking, solver obligations and
executable validation did not establish faithful interpretation of the question.

## Findings for the symbolic path

Five of the grammar's twelve missed supported requests were stopped by the
creation-word requirement: variants beginning with I need, Give me, I want or
I enter did not contain a required create/make/build-style word. Explicit code
mode already supplies a programming goal, but the grounder demands another
lexical creation signal. This is a concrete interface/grounding restriction.

Other failures expose incomplete semantic handling of modifiers and relations,
including separate actions, use/using, change expressed as goes down, descriptive
unit-converter language and multiline. Numeric initial values, negation and
action-target scope also need explicit, traceable roles. The independently
checked model successes provide useful examples of backend-expressible contracts;
they do not require retaining a model at runtime or turning whole prompts into
special cases.

The previously failing mixed sentence was accepted by the resident grammar during
this comparison. A separate cold CLI control still rejected it with loc_nonsp,
while the reordered sentence generated successfully. This observed sensitivity
to parser/search context needs investigation. The results do not identify a
specific cache bug or prove that ignoring one predicate would fix it.

The next symbolic work should carry explicit request intent into grounding,
represent operations/targets/initial values/operands/units/exclusions as typed
requirements with source evidence, and improve grammatical role binding and
constraint checking. Parse traces should expose candidate limits and rejection
reasons. Ambiguity should remain visible. The 32 annotated cases can serve as a
development regression gate, supplemented by separately frozen new paraphrases.
Interpretation fidelity and specification-to-executable correctness need their
own checks. No interpreter fixes were made as part of this experiment.

## Isolation and reproduction

The implementation is under archive/local_interpreter. It requires an explicit
model argument or environment variable and does not download models. Its extra
JSON Schema dependency is installed under the ignored .deps/interpreter-experiment
directory, with a separate version lock. Seven protocol/contract tests pass,
including arbitrary model selection, typed operands, rejection of incomplete
requirements, source evidence checks and truncated-response handling. These
protocol tests are separate from the model-quality scores above.

The original checkout's 151 baseline tracked paths and existing diff remain
unchanged. There are no unexpected changes to carried user edits. Reproduction
commands and configuration controls are in the
[experiment guide](../archive/local_interpreter/README.md).

Raw local reports: [combined summary](../.benchmarks/local_interpreter_summary.json),
[grammar and 4B](../.benchmarks/local_interpreter_experiment.json),
[8B](../.benchmarks/local_interpreter_experiment_8b.json), and
[diagnostic apps](../.benchmarks/local_interpreter_demos.json). These are ignored
runtime artifacts. The frozen [cases](../archive/local_interpreter/cases.json)
and experimental implementation remain available for inspection.
