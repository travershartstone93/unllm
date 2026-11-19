# Native migration implementation evidence

The default symbolic build and the working `build/unllm` use the C++ host. The first-party host covers interpretation, reasoning, construction, both code emitters, validation, ingestion, ranked search and update transactions. The installed package excludes the earlier Python host and Rust search executable. The coding-capability roadmap follows this migration.

## Isolation and baseline

`.state/cpp-migration/baseline/` contains the pre-port source archive, SHA-256
manifest, tracked/untracked status, carried diff, Git revision, executable/grammar
hashes and CTest log. The two sealed evaluation files were excluded and remain
uninspected. All 12 pre-port CTest suites passed in 275.61 seconds. This does not
replace the separate manual-output, interpretation, performance and deployment
gates in the migration plan. The fresh independent reference run also passed all
12 application outputs and 78 GUI/file/timer assertions. The 32-case grammar
comparison reproduced 20 correct, 12/24 supported requests, 8/8 safe stops and
zero wrong acceptances. These are hybrid reference results, not native application
parity results. See `baseline/independent-verified.json` and `interpretation.json`.

The [command/dependency inventory](CPP_MIGRATION_INVENTORY.md) records supported
profiles, native/target/reference dependencies and the first-party Rust search
contract. Upgrade SQLite databases were backed up with SQLite's native backup
API. The original sibling's carried diff still hashes to
`1f393cb2ee62d323cae04ee6a94b3be3b8f43e323f84665734bc41b2fedaaf2e`.

## Implemented so far

- Native dependency setup and separate CMake build. The cvc5 1.3.1 SDK archive is
  pinned by its published SHA-256 in `dependencies-native.lock.json`.
- Native JSON boundary helpers, canonical serialization/SHA-256, ICU keys and
  a bounded process supervisor with simultaneous stdout/stderr draining.
- ANTLR C++ parsers for the ACE envelope, SimpleMRS and provenance terms.
  Persistent ACE requests retain separate grammar processes.
- Typed facts/goals/relations and SQLite source replacement, validity, aliases,
  read snapshots, revisions and checksum-protected prepared caches.
- A compiled resident inference worker using the existing SWI-Prolog/Soufflé
  bridge. Each worker owns its embedded engine; the C++ parent enforces deadlines
  and resets failed workers. No Python host or runtime fallback is involved.
- A separate C++ proof checker for source, background, dependency and numeric
  witnesses. Answer bindings and query obligations are checked explicitly.
- Factual MRS translation, dictionary entity linking, answer composition and
  bounded conversation contexts. Corrections, undo, explicit completeness,
  reference resolution and hypothetical overrides run through the development
  C++ symbolic worker.
- Typed scalar-expression and stream-program specifications; ANTLR expression
  parsing; native cvc5 synthesis; component planning with independently checked
  proofs. Native GMP/MPFR convert rational solver constants to binary64; the
  loaded GMP version is checked against its headers.
- C++ emitters for stream programs in both Python and C++, a separate C++
  contract interpreter, bounded sandbox execution and a development generation
  command. C++ outputs compile in normal and ASan/UBSan configurations. Bounded
  scalar repair restores specified evaluation order if executable checks fail.
- `resources/targets/streams/common.py` is generated-program source data, read
  by the C++ emitter. It is never imported by the host. The optional target
  interpreter is used only to run generated Python programs.
- The native facts command imports JSON, reference packs and cached Wikidata
  records, reports database counts and prepares reusable inference snapshots.
  Packs with two candidate identities for a name now skip the ambiguous relation.
  Tests used offline Wikidata fixtures. Live HTTP import remains unverified.
- The native shared application path parses typed specifications and expressions,
  evaluates expressions, synthesizes simultaneous state transitions with cvc5
  and constructs handlers from verified provider plans. Its Python and C++ GUI
  emitters generate standalone Qt applications. A C++ validator drives real
  widgets, checks state, labels, timer traces and files, and runs the C++ drivers
  again with ASan/UBSan. A separate C++ checker judges both output languages.
- The schema 1 calculator path constructs a controller, enabled operations,
  widget grid and keyboard bindings from checked component providers. Native
  cvc5 synthesizes the optional memory updates and the division guard. Targets
  use the staged ANTLR arithmetic parser. The native validator generates its
  arithmetic inputs and expected values independently of that parser.
- English creation requests now use ACE/MRS through the public CLI when
  `UNLLM_SYMBOLIC_RUNTIME=native` is set. Structured input, replay and revision
  share the same native worker. Generated-Python execution requires an explicit
  target interpreter. Its optional ANTLR package can be mounted separately via
  `UNLLM_PYTHON_TARGET_ANTLR4`.
- The native maintenance development tool checks FineWiki English and Simple,
  Stack Exchange, Wiktionary, WordNet and the fixed poetry source. Source
  comparisons, checksum caching and unknown-source reporting have offline
  fixture coverage.

## Checks completed

The earlier 17 native suites passed on November 18 in 42.83 seconds. The run is recorded
in `.state/cpp-migration/native-status-nov18.log`. The optional Python target
rerun also passed all 181 execution checks across ten generated programs.

| Suite | Evidence |
| --- | --- |
| Foundation | Native cvc5 synthesis, Unicode/hash/JSON vectors, process timeout/reaping, output limit, concurrent stderr drain. |
| MRS | Exact decoded graph parity against 134 frozen PyDelphin analyses, malformed input rejection, two live resident ACE requests. |
| Store | Frozen reference IDs/digests/claims, validity boundaries, rollback after a failed write, concurrent read isolation, corrupted cache rejection. |
| Inference | Transitivity, inverse/type/dependency/numeric reasoning, altered proof rejection, independent workers, prepared snapshot reuse, expired deadline and subsequent recovery. |
| Semantics | 154 frozen analyses: all 70 accepted meanings/signatures matched and all 84 unsupported analyses stayed rejected. Only traversal order within scope label lists is normalized. Live resident/fragment parsing passed. |
| Conversation | Live assertion, pronoun, conflict, correction/undo, explanation, missing evidence, completeness, hypothetical isolation and reset checks. |
| Qt | Native widget construction and a real mouse event updating state. This is a dependency probe, not GUI-generation parity. |
| Expression | Frozen expression-tree/type/rejection parity and native scalar SyGuS for decrement, fractional conversion, Boolean predicates and conditionals. |
| Program | Frozen component identities, eleven accepted specifications and thirteen rejected specifications; every accepted component plan independently proof checked. |
| Stream emitters | Ten Python outputs passed 181 isolated oracle cases. Ten C++ outputs passed 362 checks across normal and sanitizer builds. The native oracle was also compared against all 181 frozen reference cases; final combined checks are in the current log. |
| Stream job | A new specification completed native planning, SyGuS, compilation and independent execution. Deliberately emitting `x+11` against the requested `x-11` contract was rejected. Python emission worked with no target interpreter supplied. |
| Importers and facts CLI | Pack import and five offline Wikidata scenarios matched the reference. Command tests cover import, status, prepare, cache reuse and errors. A manual query reused the prepared snapshot and cited both imported premises. |
| Behaviour expressions and specifications | 98 expression cases and 50 application specifications matched the reference. The spec corpus includes 21 expected rejections. Accepted expression trees were also evaluated against reference outputs. |
| Behaviour solver and planner | Six transitions passed 46 numerical checks. A contradictory transition failed. Eight plans matched the reference across both target languages. Removing the atomic writer prevented a save plan. |

A fresh manual factual conversation inferred Mira's location through London and
England, listed both premises, withdrew support after correction and restored it
after undo. A fresh structured stream request composed filtering, decrement,
deduplication, sorting and statistics; the native command emitted a 71-line Python
program and validated it on 37 cases. Neither observation establishes general
algorithm synthesis or overall speedup.

Captured Python reference outputs are development fixtures only. New tests and
host modules are C++; the default native test graph does not launch Python.

## English and GUI acceptance on November 18

| Boundary | Measured result | Evidence under `.state/cpp-migration/` |
| --- | --- | --- |
| English grounding | All 32 live requests matched the frozen reference disposition and every accepted specification. The original task oracles still score 20/32, including 8/8 negative cases. | `ground-parity.log`, `baseline/interpretation.json` |
| Public CLI | The six original English prompts each produced an independently exercised Python and C++ app, with 39 UI assertions per language. The CLI also validated a counter that decrements by 13. | `gui-trial/public-{python,cpp}.log`, `english-trial/decrement13.json` |
| Calculator | All four typed configurations passed in both languages. Across four Python apps the validator checked 500 arithmetic and GUI outcomes. Across four C++ apps the normal and sanitizer builds checked 1,000. | `calculator-trial/{cpp,python}/*-result.json` |
| Behavior composition | Each language passed the four fixed specifications plus a new specification with generated coefficients. Fresh interaction cases followed ordinary validation. Both validators caught a deliberately reversed decrement. | `gui-trial/compositions-python.log`, `native-extended.log` |
| Worker protocol | Replay, revision, recovery after an invalid revision, context isolation, reset, mixed factual/programming requests, malformed JSON, size limits and truncated frames passed. | `protocol-test.log` |
| Source checks | Six source profiles passed local metadata fixtures covering current, changed and unavailable sources. | `source-checks.log` |

The expanded 26-suite native run passed in 238.11 seconds
(`install-trial/native-regression-all.log`). The source-check fixtures use no
live sources or installed packs.

## Relocated installation checks

The native CMake installation includes the symbolic worker, logic worker, ACE,
ERG, native solver libraries, reasoning data and target resources. A probe ran
the staged prefix at `/opt/unllm` with a fresh writable state directory, an
allowlist of system libraries and a C++/Qt toolchain. The checkout and Python
runtime were absent from this filesystem.

- The worker recorded a factual assertion and answered a follow-up. It emitted
  Python calculator source, then compiled and exercised a C++ calculator. Its
  normal and sanitizer builds passed 254 arithmetic and GUI checks in total.
- The separately built public CLI was copied into the staged prefix for this
  test. With `UNLLM_SYMBOLIC_RUNTIME=native`, it located the installed worker
  beside itself and passed the same 254 C++ checks from an English prompt.
- The installed facts command imported a fixture and prepared 41 tuples. A
  fresh worker read the same user-state database and answered “Is Northport in
  Vale?” with a supported result. The process traces passed the Python-launch
  and shared-library checks.

The release probe is `tests/cpp_migration/installed_runtime.sh PREFIX NEW_DIR`.
The public command is exercised when `PREFIX/bin/unllm` is present. This run's
reports and traces are under `.state/cpp-migration/install-trial/combined-isolation/`.

The package now builds the public CLI and native workers together. Build, install and existing-pack conversion instructions are in [the upgrade guide](UPGRADE.md). The working checkout and an installed prefix use separate default state paths. Set `UNLLM_FACT_DB` to select an existing database explicitly.

## Warm factual-query latency

The resident C++ worker now reads and validates the prepared SQLite snapshot
when it needs to load or restore a reasoning world. A repeat query on the same
resident world skips that work. A native regression test counts snapshot reads
for a warm hit and after a worker reset.

Each release-build batch replayed 30 pairs of identical structured fact queries
against copies of the same database, alternating which worker answered first.
All 90 answer pairs matched after the change.

| p95 in milliseconds | Batch 1 | Batch 2 | Batch 3 |
| --- | ---: | ---: | ---: |
| Native before | 8.09 | 7.94 | 8.01 |
| Reference in those batches | 8.32 | 7.74 | 8.13 |
| Native after | 6.56 | 6.56 | 1.89 |
| Reference in those batches | 7.72 | 8.77 | 2.14 |

The reference timing also moved between batches. These figures cover a warm
worker protocol round trip for one structured factual query. Cold start,
English parsing and code generation have separate performance gates. The input
and individual samples are under
`.state/cpp-migration/install-trial/latency-release-{before,after}/`.

## Release results

- A clean configure, build, install and 33-test run passed on Linux x86_64 in a namespace without Python or Cargo, with staged native SDKs and system libraries. The build and installed-package probes record process traces.
- Isolated installed runs covered default symbolic, legacy and shadow operation, importing and preparing facts, C++ calculator compilation and sanitizer execution, Python source emission, and ranked and FTS pack search. An explicitly configured Python target separately passed 127 calculator checks.
- In 108 saved ranked searches on a seven-passage fixture the C++ adapter reproduced every Tantivy result in order. The largest score difference was about 0.000001. Existing packs need a one-time native index build. Full-scale corpus ranking and live network refresh were outside these fixture runs.
- Native maintenance tests cover Parquet, 7z/XML, gzip poetry, dictionary and local document ingestion, parallel shards, quality checks and TSV scoring. An offline update downloaded fixture input, checked its hash, encoded a real shard, rebuilt indexes, rejected a lower-scoring candidate, published an acceptable candidate and rolled back. Transaction tests forced 13 process exits and exercised an external vector index location.
- Across three batches of 30 paired inputs each, all eight measured host stages met the plan's p95 tolerance. Native solving took roughly 21–29 ms at p95 versus 18–24 ms for the reference. Cold single-request launches measured 0.10–0.26 s native and 0.17–0.38 s reference. One end-to-end calculator run took 24.34 s native versus 25.91 s reference. Peak child RSS was 405,360 and 428,544 KiB respectively. The calculator run includes compilation and validation and supplies only one observation per version.

The working CLI passed all 33 suites. In a separate four-turn trial it recorded two location assertions, answered the transitive question and cited both premises when asked why. The standalone ingest probe also installed, checked, forcibly replaced, removed and reimported a fixture pack via the inbox. Multi-shard restart tests retained the completed SQLite shard across an interruption.

In this working copy, the files `release-final.path`, `cutover-installed.path`, `target-final.path`, `final-fts.path` and `cli-check.path` under `.state/cpp-migration/` point to the frozen build logs, process traces and target or CLI results. `stage-trial.path` and `end-to-end.path` locate the timing samples.

Reproduce a release check with a fresh evidence directory for each command.

```sh
tests/cpp_migration/clean_build.sh "$PWD/.state/clean-release"
tests/cpp_migration/installed_runtime.sh "$PWD/.state/clean-release/prefix" "$PWD/.state/installed-release"
```

To include update scoring and real encoding in the installed test, set `UNLLM_TEST_REFRESH_FIXTURE` to the absolute path of the compiled `native_refresh_fixture` and `UNLLM_TEST_EMBED_MODEL` to a local BGE embedding model directory. Fixture runs leave production packs in place. The historical comparator wrote its normal index-progress file under the user's data directory.

The baseline source archive and saved hybrid executable under `.state/cpp-migration` preserve the earlier version for explicit whole-version comparisons. The package never invokes that reference on a failed request.

## Roadmap review after the port

The native baseline preserves the interpretation result of 20/32 on the existing request set. A useful next comparison is the Stage 13 numeric pilot against an engineering-only baseline, scoring wrong acceptances and unnecessary clarifications as well as correct requests. In parallel, Stage 14 can exercise a typed loop and checked integer-sequence operations from a supplied specification. Compare the interpreter, generated Python and generated C++ on identical inputs. Stage 15 then freezes the primitives and checks whether the engine can discover the structure for an unfamiliar task. This separates gains in understanding English from gains in writing algorithms. [Foundational roadmap](CODING_CAPABILITY_ROADMAP.md)

The storage proposal adds a distinct prerequisite for programming packs. Budget for the existing and newly built ranked indexes simultaneously during migration, and verify that all readers and writers resolve the same mounted volume. New programming packs can then be compared against synthesis alone on a small provenance-tracked pilot. The proposed project-trained model, multi-toolkit dashboard and repository-editing experiments come later. [Storage and corpus update](CORPUS_STORAGE_AND_EXPANSION_UPDATE.md) · [Future coding roadmap](FUTURE_CODING_CAPABILITY_ROADMAP.md)
