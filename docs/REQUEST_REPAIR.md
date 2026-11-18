# Request interpretation and executable repair

Implemented and checked on 2026-11-12 in the separate unllm-upgrade copy.

The six unchanged English requests from [the earlier manual trial](MANUAL_TRIAL.md)
now generate working applications in **both Python and C++**. All twelve outputs
pass an independent keyboard/mouse/file workflow, in addition to their ordinary
validation. The previous trial generated two of six and exposed a real C++ input
bug. Those historical failures remain in its report.

## Observable results

Every row used the actual CLI, an isolated fact store and the model disabled.
The [request fixture](../tests/upgrade_requests.json) is byte-for-byte identical
to the earlier trial's questions. The workflows and their expected results are
separate from the generation inputs and built-in outcome oracle.

| Original request | Python | C++ | Independently exercised behavior |
| --- | --- | --- | --- |
| Editable text area, character count, lowercase and save buttons | Pass | Pass | Unicode/emoji paste, live counts, lowercase, exact UTF-8 save/overwrite, filename reuse, cancelled close preserves edits |
| Counter decreases by 11 | Pass | Pass | Keyboard input 17.5 → 6.5, 0.25 → −10.75, −100 → −111 |
| To-do list with add/remove/save | Pass | Pass | Trimmed and Unicode entries, selection and checkboxes, JSON contents, removal, empty-entry rejection, cancelled close |
| Stopwatch with start/stop/reset | Pass | Pass | Real elapsed time, paused value stays fixed, resume accumulates, reset while running or stopped |
| Celsius to Fahrenheit | Pass | Pass | 100 → 212, 17.5 → 63.5, 0.25 → 32.45, −40 → −40 |
| Tip calculator with bill and percentage inputs | Pass | Pass | Variable percentage, zero rate, small decimals, tip and combined total |

These are **12/12 independently passing outputs**, with 78 observed assertions.
For example, bill 123.45 and rate 12.5 produce tip 15.43125 and total 138.88125.
The independent harnesses drive Qt widgets and inject dialog decisions through
the application's service boundary; actual file operations execute normally.
They do not invoke the internal validator's scenarios or reference evaluator.

Five follow-up requests also pass: reversed Fahrenheit/Celsius conversion;
a shopping list with add/delete/open/save; a discount with a variable rate;
a timer combined with editable text and a live character count; and text,
a counter increasing by three, and a timer together. These follow-ups informed
repairs to shopping-word linking, nested response transport and compound-request
parsing. All results here are development evidence, not a blinded evaluation.

The complete CTest run passes **12/12 suites**, including the six original suites
and **64 Python tests across the six upgrade suites**. Their summed execution
time is 208.54 seconds. Additional explicit-contract compositions are recorded in
[the behaviour report](BEHAVIOUR_CONSTRUCTION.md).

A subsequent [unchanged-generator retest](REQUEST_RETEST.md) repeats those passes
and finds a remaining weakness: a longer text/counter description rejects, while
reordering the same requirements produces a working app. The new failure is
recorded separately and does not change the scope of the six-request result.

## What changed

### Input and validation

The C++ numeric view used to call setValue on every refresh, even when its value
already matched the model. That reformatted incomplete keyboard input and removed
the decimal point. Its refresh now compares values before updating the widget.
Both internal drivers type numeric values through Qt key events; the validator
includes positive/negative fractional input. Separate external probes retain the
original failing 17.5 and 0.25 examples.

### Interpretation and grounding

The shared English path now builds typed frames from ACE/ERG semantic arguments,
compound relations, conjunctions and bound operands. Action targets and unit
direction are resolved from those structures. Ambiguous targets and unsupported
content reject instead of silently disappearing. Character-count readings must
contain a bound counted quantity.

[The domain definitions](../data/program_semantics.json) declare concepts,
action labels, record schemas, percentage roles and affine units. A token/span
linker gives unfamiliar domain nouns and verb-shaped button labels grammatical
surfaces that ERG can analyze, retaining their original concept and source spans.
This is finite lexical grounding combined with a real grammar, not a set of
whole-prompt regular expressions. It still requires declared vocabulary.

The early calculator-name branch was removed from language.py. A tip request now
grounds amount/rate/result requirements into the shared constructor. A bare
standard calculator can still request the existing expression-calculator
capability; its compatibility controller remains. That capability cannot silently
swallow other requested goals.

Explicit --code requests use a separate lazy ACE process with a four-second parse
budget. A longer mixed request needed 1.45 seconds of grammar search and had
previously failed the one-second budget. Normal factual and implicit requests
retain their one-second parser budget and separate three-second CLI deadline.

### State and effects

Schema 2 now supports flat typed record collections, selection and optional
Boolean check fields, append/remove/update operations, and strict UTF-8 JSON
persistence. Saves and loads use the same generic file contracts as text. Loading
the wrong record schema rejects without replacing the existing state.

Clock capabilities use monotonic elapsed time, explicit running/anchor state and
periodic events. Start is idempotent, stop freezes the elapsed value, resume
accumulates time and reset stops and zeros it. Editable text and numeric controls
can coexist with the periodic event. The clock service is injectable for bounded
tests; ordinary generated apps use the host monotonic clock.

Effect planning now searches an operator prerequisite graph for a requested
outcome. For example, a clean saved state depends on encoding, path selection,
successful atomic writing and recording the saved snapshot. The planner resolves
these dependencies with the native Prolog provider search; the constructor lowers
the selected steps into explicit handlers. Availability, ordering and goals are
checked. Soufflé continues to check typed wiring. Programs contain reusable widget
and file helpers, rather than a controller chosen by an application label.

### Scalar synthesis and transport

cvc5 can synthesize bounded degree-two polynomial-real updates, including
100 * next.part == prev.amount * prev.rate. The definitions supply percentage
roles and whether the final total adds or subtracts the part. Unit conversion
derives an affine equation from unit scales and offsets. Guards become runtime
preconditions; supported numeric guards also constrain the solver obligation.
More complex scalar goals remain unsupported. Formal obligations concern
mathematical reals; execution checks cover binary64 behavior separately.

A list with both open and save produced a valid response whose planner witnesses
had JSON depth 35. json-c's default limit of 32 rejected that response in the
actual CLI. The transport now has an explicit bounded depth of 128, with a real
CLI regression for the open/save composition. This explains why testing only the
Python Engine would have missed that failure.

## Run or reproduce

The [exported request examples](../examples/upgrade/requests/README.md) contain
both languages, specifications, screenshots and source hashes:

```sh
./examples/upgrade/requests/run.sh todo python
./examples/upgrade/requests/run.sh stopwatch cpp
./build/unllm --code python 'Build a tip calculator with bill amount and tip percentage inputs.'
```

The launchers need a desktop display. Generated applications need only their
Python/Qt dependencies or Qt runtime libraries. They do not contact unllm or a
model. Reproduce the checks with:

```sh
python3 tools/setup_upgrade.py --check
ctest --test-dir build --output-on-failure
.venv/bin/python tools/verify_requests.py
.venv/bin/python tools/export_request_examples.py
```

The final request run took a median 1.826 seconds for Python and 26.173 seconds
for C++, including validation. Ranges were 1.349–2.545 and 25.188–27.922 seconds.
Other regression work ran concurrently, so these are observed job timings, not a
controlled latency comparison. C++ still compiles ordinary and sanitizer checks.
There is no claim of a large speed improvement from these functional repairs.

Raw local reports: [CLI generation](../.benchmarks/request_fix_trial.json),
[independent workflows](../.benchmarks/request_fix_verified.json),
[follow-up compositions](../.benchmarks/request_fix_fresh.json), and
[regressions](../.benchmarks/request_fix_regression.json). The reports and job
directories are ignored runtime artifacts. Reproduction requires the pinned local
toolchain described in [the guide](UPGRADE.md); no new dependency was added.

The saved-example launchers also pass offscreen startup checks for both languages,
including rebuilding a C++ executable from its exported source. Source hashes
match all 22 new and regenerated exports. The final [isolation audit](../.benchmarks/request_fix_isolation.json)
matches all 151 baseline tracked paths in the original checkout, its HEAD and its
existing diff, and finds no unexpected changes to carried user edits.

## Remaining scope

This extends composable capabilities and improves English grounding. It does
not supply arbitrary algorithm discovery, unknown API use or unrestricted English
understanding. New combinations must fit the declared semantic roles, state types,
operators and expression grammar. Records are flat, with at most 10,000 items and
16 fields per schema; periodic events are bounded to eight per program. The
existing scalar, file, state and job limits still apply. Explicit specifications
remain useful where English is ambiguous or outside the linked vocabulary.

Finite executable validation, component witnesses and mathematical scalar proofs
have different coverage. Passing them does not prove every GUI sequence or
floating-point input. The separate blind release gate remains open. Changes and
generated examples are confined to unllm-upgrade; the original sibling and its
pre-existing edits remain preserved.
