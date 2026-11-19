# Symbolic unllm upgrade

This working copy adds a model-free reasoning and program-construction path. The original `../unllm` directory and ActivityScope's copy are separate. Run this copy's `build/unllm` explicitly.

The runtime uses ACE/ERG and native ANTLR parsers for English semantics, SQLite for source claims, compiled Soufflé for background relations, embedded SWI-Prolog for queries and interactive rules, and cvc5 for bounded scalar synthesis. C++ emits both target languages from typed specifications. Clang compiles and checks C++ output. Calculator applications use ANTLR for arithmetic parsing and Qt for their windows. There is no generative fallback in symbolic mode. [REQUEST_REPAIR.md](REQUEST_REPAIR.md) records the latest English-to-application checks; [UPGRADE_REPORT.md](UPGRADE_REPORT.md) records the earlier reasoning and utility measurements.

## Run the installed working copy

```sh
cd /home/whisper/repo-work/unllm-upgrade
tools/setup_native.sh
cmake -S . -B build -DUNLLM_PACKAGE_NATIVE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
UNLLM_NO_SERVER=1 ./build/unllm
```

Enter one sentence per line. For example:

```text
Maya Chen is in Harborview.
Harborview is in Caldera.
Where is Maya Chen?
Why?
Maya Chen depends on Rowan Patel.
Rowan Patel is not available.
Is Maya Chen blocked?
:complete depends_on "Maya Chen"
What if Rowan Patel is available?
Is Maya Chen blocked?
No, Rowan Patel is available.
Is Maya Chen blocked?
```

The location answer includes both places and exposes both premises. The hypothetical changes a temporary world; the next ordinary question still uses the original facts. The correction changes only conversation assertions. Establishing that every requirement is satisfied needs the explicit completeness boundary.

Without `UNLLM_NO_SERVER=1`, the CLI uses its own resident service. Its socket is derived from this executable's directory and user ID. The client checks build and configuration identity before using it. `--json` includes the serving executable, process, native library, rule version, typed interpretation, proof and sources. One-shot calls start fresh conversations. Assertions are session state; import a source document to retain facts across conversations.

`--engine symbolic` is the default in this copy. `--engine legacy` runs the original retrieval/model path; `UNLLM_MODEL=off` disables its generative answerer. `--engine shadow` retains legacy output and attaches the symbolic result for comparison. An explicitly requested symbolic mode fails clearly in a build configured with `UNLLM_SYMBOLIC=OFF`.

## Add structured knowledge

```sh
./build/native/unllm_facts_native status
./build/native/unllm_facts_native --database .state/demo.sqlite import-json examples/upgrade/facts.json
./build/native/unllm_facts_native --database .state/demo.sqlite prepare
./build/unllm --facts .state/demo.sqlite 'What is the country of the birthplace of the author of The Copper Almanac?'
```

The example source is fictional and is labelled that way. Every document has a stable source ID, entities and claims; importing that ID again atomically replaces its claims. Use namespaced entity IDs. Declared type IDs such as `city` cannot be reused as ordinary entities. Multiple entities may share a name; the runtime asks for clarification instead of merging them.

In the checkout the default store is `.state/facts.sqlite`. An installed worker uses `$XDG_STATE_HOME/unllm-native/facts.sqlite` or `~/.local/state/unllm-native/facts.sqlite`. `--facts` or `UNLLM_FACT_DB` selects another. SQLite uses WAL and each factual request holds one read snapshot through interpretation, evidence checks and rendering. Imports preserve source metadata and explicit claim polarity. Optional `valid_from`/`valid_to` are Unix seconds with a half-open interval. Unsupported semantic qualifiers are rejected. A `source_location` qualifier is provenance metadata and is accepted.

Adapters also support exact structured values from installed packs and a bounded Wikidata import:

```sh
./build/native/unllm_facts_native import-packs --titles Paris France Frankenstein 'Mary Shelley'
./build/native/unllm_facts_native import-wikidata Q150827 Q47152 Q90 Q142 --depth 1 --max-entities 30
./build/native/unllm_facts_native prepare
```

Only the Wikidata import needs network access; `--offline` uses its existing `.state/wikidata` cache. Answering does not fetch facts from the network. Pack access is read-only. The adapters deliberately skip ambiguous infobox text and unsupported qualified Wikidata statements. This produces a small structured knowledge base, rather than treating every retrieved sentence as an accepted fact.

Preparation publishes a checksummed, versioned closure in SQLite. Changed claims, validity, relation definitions or rules invalidate it. Derived explanations are reconstructed with the compiled Soufflé program on first demand and independently checked against the current premises. Preparation does not eliminate that first proof-construction cost. Session changes and hypotheses use their own snapshot keys.

## Supported reasoning

The initial relation vocabulary covers locations and inverse containment, country membership, types and type inheritance, authorship, birthplace, dependencies, availability and blocked status. Queries compose these relations over unfamiliar registered names. Positive and negative evidence are separate: an answer can be supported, refuted, conflicting, or unknown. Absence is not a negative fact.

The dependency rule explains which unavailable requirement blocks something, follows transitive dependencies, and reports unknown availability premises. Numeric comparison currently covers `taller_than` from explicit height facts. Imported heights must already be finite decimal metres; numeric properties use a bounded binary64 contract within ±10^15. Direct calculations and unit conversions continue through the existing calculator.

Counts report the number of supported matches and state that the set may be incomplete. General universal quantification, disjunction, scientific causal inference and arbitrary historical or qualified statements are outside this version. English has a bounded interpretation search; ambiguous and unsupported requests remain explicit. Unsupported factual language may return a labelled reference excerpt from installed packs, with no claim of a logical derivation.

Commands:

| Command | Effect |
| --- | --- |
| `:why` or `:evidence` | Repeat the previous factual query with its premises. |
| `:missing` | Repeat it with supported missing-premise diagnostics. |
| `:undo` | Undo the last conversation assertion/correction. |
| `:reset` | Clear the conversation's facts, boundaries and program state. |
| `:complete depends_on "name"` | Declare this conversation's requirements boundary. |
| `No, ...` / `Actually, ...` | Correct a matching conversation assertion. |

The native module accepts up to 100,000 input claims and 500,000 derived tuples, with a conservative graph-size check. Queries have bounded inference, solution and proof-depth budgets. The normal CLI deadline is three seconds. A resource failure cannot establish that a proposition is false.

## Generate Python or C++

For a callable integer-sequence function, pass input and output types and input-output examples through `--code`. Include the empty input and represent signed integers as decimal strings. The search can return `exhausted`. A validated function has passed the supplied examples in the requested target. [Example and reserved-case checks](../archive/interpretation_diagnostics/n16/bridge.md)

```sh
./build/unllm --code python 'Write a program that reads positive numbers, triples them, and prints their sum.'
./build/unllm --code cpp --spec examples/upgrade/record_statistics.spec.json
```

Results identify a source file and `report.json` inside `.artifacts/job-*`, with build/run commands, interpreted specification, assumptions, component proofs, solver results and executable checks. Stream utilities read standard input or one optional filename and write results to standard output. Inspect the interpreted specification as well as the code.

In an interactive conversation, these commands regenerate and revalidate the last program:

```text
:revise {"output":"mean"}
:code cpp
```

The supported families are numeric streams, text lines and `key TAB number` records. Operations include numeric filtering/mapping, stable deduplication, sorting, reversing, lowercase/trim/word splitting, grouped aggregates and frequencies. Operations compose through a typed representation; the catalog contains reusable contracts rather than complete answers to prompts. Scalar expressions use `x`, bounded constants, `+`, `-`, `*`, comparisons, boolean conditions and conditional expressions. Filters must return booleans; maps must return numbers. Some English requests need a typed specification to resolve scope or operand order.

A compact explicit specification is:

```json
{
  "input": "records",
  "steps": [
    {"op": "filter", "expr": "x >= 0"},
    {"op": "map", "expr": "3*x + 1"},
    {"op": "unique"},
    {"op": "sort", "descending": true}
  ],
  "output": "stats",
  "invalid": "skip"
}
```

`input` is `numbers`, `lines` or `records`. `output` is `values`, `sum`, `mean`, `min`, `max`, `count`, `stats` or text `frequencies`. `invalid` is `error` or `skip`. Unknown fields, incompatible operation types and lost expression arguments are rejected. A revision replaces the fields it names; replacing `steps` supplies the complete new operation list.

Programs accept printable ASCII and TAB, optional CRLF, at most 100,000 rows, and at most 16,384 characters per line. Numbers and final numeric outputs must be finite within ±10^15. Sorting is case-sensitive, deduplication keeps first occurrences, and record groups are emitted in key order. Numeric blank lines are ignored. Text blank lines are data. Empty numeric sums are zero; other empty numeric aggregates are errors. Record groups emptied by a filter are omitted. Invalid input reports `error:` on stderr, returns 2 and emits no partial results; `skip` applies to invalid rows, never to the total row budget.

The initial synthesis job budget is 180 seconds, separate from the factual-answer deadline. cvc5 checks the declared bounded scalar fragment over mathematical reals. Independent executable checks cover floating-point behavior; a solver-equivalent expression can need repair to preserve evaluation order. Python and both normal and sanitizer-enabled C++ executables run in bubblewrap with no network or writable checkout access. These are tested programs, not unrestricted correctness proofs or arbitrary app generation.

## Construct a program from behaviour contracts

The schema-2 path has no application-name whitelist. Define typed state, observable views and required outcomes; the planner resolves operator prerequisites and cvc5 synthesizes relational scalar updates. Number/text/Boolean states, flat typed record collections, text and numeric inputs, list views, derived labels, clock events and file/edit effects can compose in one program. English uses linked domain definitions and ERG semantic frames with typed role binding.

~~~sh
./build/unllm --code python 'Make a basic text editor.'
./build/unllm --code python 'Build an app with a text area, a counter, and uppercase and save buttons.'
./build/unllm --code cpp 'Build an app that increases a counter by 7.'
./build/unllm --code python 'Make a to-do list app with add, remove, and save buttons.'
./build/unllm --code cpp 'Create a stopwatch app with start, stop, and reset buttons.'
./build/unllm --code python 'Build a tip calculator with bill amount and tip percentage inputs.'
./build/unllm --code cpp --spec examples/upgrade/behaviours/mixed_workspace/specification.json
~~~

In an explicit schema-2 specification, program is an arbitrary label. Actions may declare a relational postcondition such as `2 * next.quantity == prev.quantity + 6`, assignment outcomes such as `upper(strip(notes))`, or a contracted effect on a selected state. The same constructor handles new labels, constants and combinations. Views and actions are data, not registered application categories.

The scalar grammar now includes degree-two polynomial-real goals, such as `100 * next.part == prev.amount * prev.rate`. An optional Boolean `requires` is checked before an action changes state. Supported numeric guards also restrict the solver obligation; unsupported guard forms retain the stronger unconditional scalar obligation. Formal proofs concern real arithmetic; separate executable checks cover finite binary64 behavior.

Record states declare a flat schema and an initial array, for example `{"type":"records","fields":{"text":"text","done":"bool"},"initial":[]}`. A `record_list` view binds that state, a numeric selection state, a text display field and an optional Boolean checked field. Collection effects are `append_record`, `remove_record` and `set_record`; appended fields are typed expressions. `records_count` and `count_true` provide derived labels. Record states use the existing load/save/save_as/new effects with strict UTF-8 JSON encoding; incompatible loaded schemas reject. Assignments between collections require matching schemas.

Clock effects are `start_clock`, `stop_clock`, `reset_clock` and `sample_clock`, bound to elapsed, running and anchor state. An event such as `{"kind":"periodic","interval_ms":25,"action":"tick"}` invokes a declared action; `visible:false` hides a timer action's button. The ordinary runtime uses a monotonic clock. Reset stops the timer and sets elapsed time to zero. At most eight events are allowed, with intervals from 10 to 60,000 milliseconds.

See [the generated list specification](../examples/upgrade/requests/todo/specification.json) and [timer specification](../examples/upgrade/requests/stopwatch/specification.json) for full examples. Records allow at most 10,000 entries and 16 scalar fields. File contents are limited to 4 MiB. Other shared limits remain 32 state fields, 48 views, 48 actions and 192 validation scenarios.

The [latest repair report](REQUEST_REPAIR.md) covers the semantic front end, operator planning and independent tests. The [architecture report](BEHAVIOUR_CONSTRUCTION.md) describes the shared backends. [Exported request outputs](../examples/upgrade/requests/README.md) and [explicit-contract compositions](../examples/upgrade/behaviours/README.md) can be launched directly. Explicit --code requests have a separate four-second grammar budget inside the 180-second synthesis job; ordinary factual/implicit parsing retains one second.

The English vocabulary and available primitive contracts remain bounded. Use an explicit specification when a description cannot be represented reliably. Arbitrary algorithm discovery and unrestricted English/code synthesis remain unsupported.

## Calculator compatibility interface

Supported calculator requests produce actual desktop applications in Python/PySide6 or C++/Qt6:

~~~sh
./build/unllm --code python 'Make a standard calculator app.'
./build/unllm --code cpp 'Create a calculator with memory buttons.'
./build/unllm --code python 'Write a calculator program without division.'
./build/unllm --code cpp --spec examples/upgrade/calculator/memory.spec.json
~~~

The default calculator has addition, subtraction, multiplication, division, decimals, unary signs, parentheses, clear, backspace and keyboard input. It uses algebraic precedence. Memory is optional. Scientific functions and arbitrary styling requirements are unsupported.

An explicit application specification is:

~~~json
{
  "program": "calculator",
  "operations": ["add", "subtract", "multiply", "divide"],
  "memory": true,
  "parentheses": true,
  "backspace": true,
  "keyboard": true,
  "precision": 12,
  "title": "Calculator"
}
~~~

All fields after program have defaults. Precision is 3–15 significant display digits. Arithmetic uses finite binary64 values and intermediates within ±10^15; internal result continuation retains 17 digits. Inputs are bounded to 1,024 characters, 256 tokens, 32 parenthesis levels and 32 unary signs. Invalid expressions, division by zero and numeric overflow display an error; Clear or a new number recovers. Unknown specification fields are rejected.

The existing revision commands also apply to applications:

~~~text
:revise {"memory":true,"operations":["add","subtract"]}
:code cpp
~~~

Each job includes the emitted controller, generated ANTLR parser, grammar/license, typed application representation, interpreted requirements, component plan and proofs, solver obligations, executable validation report and an offscreen screenshot. The run command launches a normal desktop window. Python requires the exported pinned packages; a compiled C++ application requires Qt runtime libraries. Neither generated application needs an unllm service or a model.

The [exported examples](../examples/upgrade/calculator/README.md) can be launched directly from this checkout:

~~~sh
./examples/upgrade/calculator/run-python.sh
./examples/upgrade/calculator/run-cpp.sh memory
~~~

This compatibility capability still uses its original calculator specification and controller construction. It is selected after semantic grounding of a bare expression-calculator requirement; an early calculator-name branch no longer intercepts requests such as the tip calculator. The schema-2 path above supports new behaviour compositions without registering another application family. See [the calculator report](CALCULATOR_UPGRADE.md) for the compatibility implementation and [the repair report](REQUEST_REPAIR.md) for the shared path.

## Reproduce installation and checks

The tested platform is Linux x86_64 with Qt 6, Clang, bubblewrap, SWI-Prolog, Soufflé, cvc5 and ACE/ERG. The setup script stages checksum-verified SDKs. CMake also checks for system development libraries including ICU, SQLite, Xapian, libarchive, libxml2 and md4c. The native build uses Java to generate parsers. See the dependency lock files in the project root.

```sh
tools/setup_native.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUNLLM_PACKAGE_NATIVE=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/.state/native-prefix"
```

Fresh symbolic builds select the native package by default. Run the installed CLI with `--engine legacy` for the original answer pipeline or `--engine shadow` for a comparison. Configuring the historical source build with `UNLLM_PACKAGE_NATIVE=OFF` selects the previous implementation and dependency graph. The explicit option upgrades an existing CMake cache. An installed prefix keeps programs in `bin` and packaged grammars, rules and target resources in `share/unllm`. The SQLite FTS profile can be built with `-DUNLLM_TANTIVY=OFF`. Ranked builds use the C++ implementation backed by Xapian. For existing Tantivy packs, run `unllm_index --migrate PACKS_ROOT` from the new installation to build native indexes from SQLite. It leaves the old indexes in place. Until conversion, these packs use SQLite FTS.

Set `UNLLM_PYTHON_TARGET=/usr/bin/python3` to execute generated Python. GUI targets require PySide6. Generated Python calculators also need the matching ANTLR4 target runtime, which can be supplied with `UNLLM_PYTHON_TARGET_ANTLR4`. Without a target interpreter, requesting Python produces source with an emitted status. Native C++ compilation and validation use the installed compiler and sanitizer toolchains.

The isolated build and installed runtime tests and the earlier comparison measurements are described in [the migration report](CPP_MIGRATION_PROGRESS.md).
