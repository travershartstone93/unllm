# Symbolic upgrade implementation report

Initial utility/reasoning measurements below are from 2026-11-10 in `/home/whisper/repo-work/unllm-upgrade`.

The 2026-11-12 [request repair](REQUEST_REPAIR.md) adds typed semantic grounding, record collections, monotonic clocks, variable percentages and prerequisite-based effect planning. The six unchanged requests from the manual trial now pass independent GUI/file workflows in both languages: 12/12 outputs, plus five further combinations. The complete CTest run passes 12/12 suites, with 64 Python tests across the six upgrade suites. These remain development results; the independent blind release gate is open.

The earlier [behaviour-contract refactor](BEHAVIOUR_CONSTRUCTION.md) adds a shared stateful constructor without application-category dispatch. Its five compositions pass in both languages, including fresh inputs and run-time-selected requirements. At that milestone the complete CTest run passed 11/11 suites, with 53 Python tests across the five upgrade suites. The calculator-only extension described next is retained as a compatibility path.

The 2026-11-11 [calculator extension](CALCULATOR_UPGRADE.md) adds stateful Python/C++ Qt applications, recursive component planning and an ANTLR arithmetic grammar. At that milestone the complete regression run passed 10/10 suites, with 44 Python tests across the four upgrade suites. Its four application specifications pass in both languages, including fresh GUI interactions. The nine-suite results below describe the earlier utility/reasoning milestone.

The initial bounded reasoning and Python/C++ utility implementation is working. It has real grammar, Prolog, Datalog, synthesis and executable-validation integrations. All nine test suites pass, as do the reported development benchmarks and fresh CLI conversations. The plan's separately frozen blind release evaluation remains unproven; the scores below are development results.

Start with [the usage guide](UPGRADE.md) or inspect [the generated utility examples](../examples/upgrade/README.md).

## What changed

English passes through a persistent ACE/ERG parser and PyDelphin's MRS codec. Registered entity names are linked before parsing. The translator consumes semantic predicates, arguments and scope to construct typed goals; unsupported content and different supported interpretations remain explicit. Full sentences use ERG's `root_informal` first, with a separate lazy fragment parser for short follow-ups. This removed fragment readings that had crowded legitimate nested questions out of the bounded search. The choice follows the grammar's [root definitions](https://raw.githubusercontent.com/delph-in/erg/2025/roots.tdl) and [ACE's documented root option](https://delph-in.github.io/docs/tools/AceOptions/). Learned parse-ranking weights are disabled.

SQLite stores source assertions, identities, aliases, relation declarations, validity and completeness boundaries. Compiled Soufflé derives background relations; embedded SWI-Prolog answers joins and applies interactive rules. Native witnesses are independently checked against the source claims, declared rules, query and returned bindings. An explanation includes source IDs and the completeness boundary when one is required. Positive and negative support stay separate, including conflicts and unknowns.

Conversation assertions, corrections, undo and hypotheses have their own context. A hypothetical rebuilds from its changed premises and cannot publish its conclusions into the base conversation. Each request uses one WAL read snapshot for interpretation, positive/opposite checks, labels and provenance. A test replaces a positive source with a negative version between the two evidence checks: the in-flight answer stays on version 1, and the next question sees version 2. Synthesis releases its factual read snapshot before long-running construction. A checksummed prepared closure can be published to SQLite and is keyed by facts, relation definitions and rule versions. Soufflé explanations are reconstructed on first demand and checked before use.

Program requests become typed stream/operation/output specifications. Prolog selects contract-compatible components using Soufflé's compatibility relation. cvc5 SyGuS constructs bounded scalar expressions. Python is emitted through `ast`; C++ through structured traversal of the same representation and clang-format. Independent contract checks, Hypothesis inputs and fresh executions validate the result; C++ runs both normal and address/undefined-behavior sanitizer builds. Candidate execution is isolated with bubblewrap. A floating-point counterexample can trigger regeneration from a repaired intermediate expression.

The CLI exposes symbolic, legacy and shadow modes, structured JSON, source import/preparation, code jobs and revisions. Symbolic mode bypasses generative rewriting/answering and neural retrieval models. Direct calculations retain the original calculator. Unsupported factual language can return an explicitly labelled reference excerpt.

## Verification

| Check | Observed result |
| --- | --- |
| Existing regression suites | All 6 pass: exam, dialogue, world, calc, extract, ingest. |
| New suites | All 3 pass: upgrade, synthesis, upgrade_cli; 35 Python tests in total. |
| Complete CTest run | 9/9 suites pass, 49.37 seconds. |
| Annotated reasoning development cases | 30/30 exact typed answers/statuses; no false assertions. |
| Asserted-answer precision | 25/25 supported/refuted answers correct; the remaining 5 queries correctly returned unknown. |
| Generated unfamiliar multiword names | 8/8 three-relation questions correct. |
| Unsupported-meaning checks | All 6 reject rather than discard the unsupported meaning. |
| Cyclic logical graphs | 12 generated graphs checked against an independent breadth-first traversal, with proof checks. |
| Frozen utility development tasks | 10/10 Python and 10/10 C++; three input/task families. |
| Fresh CLI conversation | 16 turns, with inspected evidence, correction, conflict, undo and hypothetical isolation. |
| Fresh code/revision execution | 4 generated programs pass separately chosen input/output checks. |
| Optional legacy build | Builds with symbolic/ONNX/Tantivy/reranker disabled; an explicit symbolic request returns a clear error. |

The reasoning fixture was used during development and includes a correction to its entity IDs. It is not a blind validation split. The generated-name cases vary names over a declared topology; the separate graph tests vary topology. The existing sealed question sets were not inspected or used for tuning. The new benchmarks do not establish broad encyclopedia accuracy or unrestricted English understanding.

The final development inputs have these SHA-256 hashes:

```text
tests/upgrade_reasoning.json
90b711fb8ccdd977e5587cabdf2c75598b7ef92858b360a30cf56c411e053eb6
tests/upgrade_programs.json
d9d4d7b476e5c30b62964ebec9b83ede3bdad1fd65226782498fdbe456244768
```

## Program capacity and size

The ten specifications cover numeric streams, text processing and grouped numeric records. They compose filters, synthesized maps, ordering, deduplication, normalization and reports. All reported validated candidates pass their executable checks; failed candidates are not promoted to validated.

Python outputs are 58–82 nonblank, noncomment lines; C++ outputs are 116–147, including required preprocessor directives. Three readable examples per language are exported with source hashes, specifications, component choices and toolchains in [the example manifest](../examples/upgrade/manifest.json). They contain real input validation, optional file input, multiple functions, branches, loops and reporting. Shared runtime helpers are ordinary reusable component code; the examples are never looked up as whole-program answers by the generator. Unused aggregate computations and constant branches were removed rather than retained to reach a line count.

This meets the useful-utility intent of the approximate 100-line goal. Python's corresponding implementations are shorter. The supported program space remains the typed utility catalog; source size does not imply arbitrary application construction.

The fresh CLI test asked for positive numbers to be tripled and summed, changed the output to a mean, and switched from C++ to Python. On newly chosen input `7, -2, 4, 0`, the executions returned `33`, `16.5` and `16.5`. A separate English request for stable line deduplication followed by reversal returned `plum, kiwi, mango` from `mango, kiwi, mango, plum`.

cvc5's scalar obligations are over mathematical reals. Executable floating-point behavior is checked separately. A test deliberately uses `(x + 10^12) - 10^12`: the solver's real-equivalent simplification changes executable results, so the first candidate fails and the repair preserves the specified evaluation order. Passing finite executable checks is not a proof of every program behavior.

## Timing and memory

Machine: Intel Core i7-10750H, 6 cores/12 threads, approximately 16 GB RAM, Linux x86_64. The symbolic path needs no GPU or generative service.

| Measurement | p50 | p95 | Scope |
| --- | --- | --- | --- |
| Warm supported English questions | 94.9 ms | 364.2 ms | 60 requests through the resident Engine on the small development store; parser, linking, inference and checked evidence included. |
| Parser time within those requests | 71.5 ms | 314.7 ms | Includes grammar retries within a request. |
| Native reasoning with explanation, 100k facts | 12.20 ms | 12.33 ms | 60 requests after loading the declared fixture. |
| Python native adapter, same 100k facts | 266.1 ms | 448.7 ms | Includes hashing the complete input snapshot on every call. |
| Independent proof check, same query | 0.214 ms | 0.227 ms | Checker already indexed the source claims. |

The 100,000-input-fact fixture contains 90,000 independent availability facts and 2,500 disjoint four-edge dependency chains. It derives 217,504 tuples and queries a four-edge path. Cold preparation and the first query took 3.83 seconds. Peak RSS for that benchmark process was 529,912 KiB, approximately 518 MiB. These measurements are for this declared sparse fixture, not every possible 100k graph.

The initial native p95 target below 100 ms and common warm-question target below one second are met on these workloads. The slowest warm English sample took 1.05 seconds. Cold loading a large world can exceed the normal three-second answer budget; batch preparation and its first proof cost must be considered separately.

Validated synthesis results took a median **1.39 seconds for Python** and **5.95 seconds for C++**, including compilation/execution/checking with the Engine resident. Maximums in the twenty-job run were 4.61 and 8.58 seconds. Each job has a separate 180-second budget. Full attempt details, compiler commands, diagnostics, solver reports and executable-case counts remain in its artifact report.

### Small comparison with the original pipeline

The same upgrade executable was run in legacy/model-enabled, legacy/model-disabled and symbolic modes against the installed read-only packs. This is a four-question smoke comparison per mode, not representative latency or accuracy evaluation. Cold means a new resident process; operating-system caches and the external Ollama service were not reset.

| Question | Legacy, qwen3:4b | Legacy, model off | Symbolic |
| --- | --- | --- | --- |
| Who wrote Frankenstein? — first request | 24.23 s | 5.96 s | 0.525 s |
| Which country is Paris in? | 6.00 s | 3.18 s | 0.117 s |
| Who wrote Frankenstein? — repeat | 2.50 s | 0.877 s | 0.020 s |
| 12 times 7 | 3.5 ms | 3.8 ms | 4.1 ms |

The symbolic factual answers were `Mary Shelley` and `France`, with checked source evidence; the calculation remained `84`. Its structured store is a deliberately small import: 84 stored claims, including ontology and entity metadata, from 16 sources with 37 entities. This speed comparison does not show equal coverage of the entire encyclopedia pipeline.

The service comparison exposed a new integration bug: a persistent worker inherited the accepted client socket, postponing the client's EOF until service shutdown. Listener/client descriptors now use close-on-exec, accepted sockets use `accept4`, and the server explicitly finishes its response. A regression uses a 60-second service lifetime and requires the response within eight seconds, so an idle-exit workaround cannot hide the failure.

### Prolog alone versus the combination

A separate goal-driven SWI benchmark loads the same 100k inputs and answers the same dependency query with a source-ID path. Its load/first query took 5.06 seconds, peak RSS was 322,820 KiB, and warm in-process table/path queries averaged about 0.002 ms at p95.

That goal-driven query avoids building unused type closure, while the combined native measurement includes C dispatch and Soufflé provenance serialization. Their timings have different boundaries and do not establish a universal speed advantage. This small warm path query favors Prolog alone. Soufflé's role is explicit reusable background derivation and component compatibility, not an automatic speedup for every query.

The measured remaining costs are grammar analysis for ordinary English, full-snapshot hashing in the large-world Python adapter, and compilation/validation for code jobs. Immutable versioned snapshots could remove repeated hashing without weakening correction and hypothesis isolation. No broad latency claim is made beyond the measured workloads.

## Limits and release status

The initial supported fragment is intentionally bounded. Comparisons currently cover explicit height facts; counts report known matches without asserting exhaustiveness. Arbitrary quantified, disjunctive, qualified, historical and commonsense statements remain unsupported. Pronoun resolution uses limited structured focus. Pack import accepts only exact supported fields; Wikidata statements with unsupported qualifiers are skipped, so an imported author does not automatically have a supported birthplace/country chain.

Synthesis supports the declared numeric, text and grouped-record utility contracts, printable ASCII/TAB input and bounded finite numbers. More languages, Unicode text, CSV schemas, arbitrary statement synthesis and repository modification need additional representations/contracts. English programming requests can require an explicit JSON specification; interpretation and executable correctness are separate concerns.

The resident service currently serializes requests. A synthesis job occupies that service until it returns; an independent `UNLLM_NO_SERVER=1` invocation can run a code job separately. Snapshot changes currently reload the native world rather than applying a minimal delta. Persisted closures still reconstruct the native proof program when a derived explanation is first requested.

Local regression, reasoning, manual-conversation, program-validation and measured performance checks are complete. The independent blind release gate in [the plan](UPGRADE_PLAN.md) remains open. General reasoning quality and wider programming coverage are not established by these development scores.

## Isolation and reproducibility

SHA-256 checks against the pre-upgrade baseline found no changes to the original directory's tracked files or diff, and no changes to pre-existing user edits carried into this copy. The original diff hash remains `1f393cb2ee62d323cae04ee6a94b3be3b8f43e323f84665734bc41b2fedaaf2e`. No original executable was installed over or replaced.

The local runtime pins SWI-Prolog 10.0.2, Soufflé 2.5, ACE 0.9.34/ERG 2025, PyDelphin 1.11.0, cvc5 1.3.1 and Hypothesis 6.170.15. The worker uses Python 3.12.13; generated Python was executed with the host Python 3.14.7. Clang/clang-format were 23.1.1, targeting C++20. The generated Soufflé bridge alone uses C++17 for the pinned native headers.

Raw local reports are [reasoning](../.benchmarks/reasoning.json), [synthesis](../.benchmarks/synthesis.json), [manual CLI](../.benchmarks/manual_cli.json), [mode comparison](../.benchmarks/mode_comparison.json), [Prolog-only comparison](../.benchmarks/prolog_only.json), and [isolation](../.benchmarks/isolation.json). They are ignored runtime artifacts. Reproduction commands, dependency checksums, import instructions and supported request forms are in [UPGRADE.md](UPGRADE.md).
