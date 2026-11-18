# Product and dependency inventory for native migration

Inspected 2026-11-13. This inventory describes the existing supported product,
not capabilities already available in the new native development build.

## Profiles and public entry points

| Entry/profile | Current implementation and dependencies | Migration obligation |
| --- | --- | --- |
| Default symbolic question, script, chat, `--serve`, `--json`, `--facts`, `--now` | C++ CLI/socket service → Python symbolic worker → Python coordination/store/semantics → native ACE and SWI/Soufflé. | Compiled worker, native coordination, compatible sessions/statuses/proofs and service identity. |
| `--code`, `--spec`, `:code`, `:revise` | Same worker; Python stream/app/behavior specifications, grounding, solver adapters, emitters and validators. | C++ construction and both emitters; Python only inside generated-Python validation. |
| `--engine legacy` | Original C++ language, arithmetic, retrieval and answer pipeline; optional native ONNX/sentencepiece and Ollama HTTP integration. | Preserve profile and optional integrations; do not introduce them into symbolic inference. |
| `--engine shadow` | Legacy and symbolic paths for comparison. | Preserve comparisons with the migrated symbolic worker. |
| `--read`, `--rewrite`, `--branch`, `--brief`, `--passages` | Existing C++ diagnostics; rewrite invokes the explicitly configured external model service. | Preserve declared diagnostics and explicit dependency/error behavior. |
| `update` (`--check`, `--dry-run`, `--source`, `--full`, `--repair`, `--prune`, `--rollback`) | CLI execs `python3 tools/unllm_update.py`. It checks/fetches sources, invokes ingest, embed/search tooling and evaluation, stages packs/indexes, switches versions and restarts service. | Native transactional maintenance pipeline, fixture-only tests, interrupted-update recovery. Do not test against live packs or stop unrelated services. |
| Structured facts/import/prepare | `tools/unllm_facts.py`, Python importers/store/native adapter. | Native command with compatible schema/source identities and prepared caches. |
| Setup and release exports/benchmarks | `setup_upgrade.py`, `setup_apps.py`, export/benchmark scripts and Python CTest modules. | CMake/C++ setup and required release orchestration; reference scripts retained outside product. |

The current `build/CMakeCache.txt` enables `UNLLM_SYMBOLIC`, `UNLLM_TANTIVY`,
`UNLLM_ONNX` and `UNLLM_RERANKER`. Cargo, ONNX Runtime and sentencepiece were found.
The final matrix must exercise these configured profiles as well as the existing
SQLite FTS alternative. Disabling them cannot establish migration parity.

## Subprocess and resource classification

| Caller | Invocations/data | Classification |
| --- | --- | --- |
| C++ symbolic client | `.venv/bin/python -u tools/symbolic_worker.py` | Host dependency to eliminate. |
| PyDelphin semantics | ACE wrapper/native ACE + ERG grammar | Preserve native dependency; replace protocol/MRS adapter. |
| Python native adapter | ctypes SWI + `libunllm_logic.so` | Replace adapter; preserve native engines and rule artifacts. |
| Scalar/behavior solvers | Python cvc5 package | Replace with pinned native C++ SDK. |
| Construction/validation | clang-format, clang++, pkg-config, bubblewrap, generated executables | C++ host orchestration; preserve compiler/sandbox/toolchain contracts. |
| Python target validation | Isolated Python runtime, PySide6/shiboken6, ANTLR runtime and generated target/probes | Optional target dependencies, never host implementation. |
| App setup | CMake/compiler/ar, Java ANTLR generator, native ANTLR runtime | Native build dependencies; generation may occur at build time. |
| Upgrade setup | Archive tools, uv, Python venv/pip checks | Retain native archive installation; remove mandatory Python host setup. |
| Maintenance | Source checks/downloads, ingest/evaluate Python, native embedding/index binaries, hashes/archive tools | Follow and port invoked Python work, not merely the top-level launcher. |
| Ingest | Python stdlib and optional format libraries, pyarrow, 7z, pdftotext, pandoc | Preserve declared input formats through native adapters; explicitly stage optional dependencies. |
| Optional legacy models | Native ONNX Runtime/sentencepiece; external Ollama service | Existing optional profile behavior, not symbolic runtime reasoning. |

## First-party Rust search contract

`search/src/lib.rs` exposes ABI version 2, uses Tantivy 0.26.2 and returns shard,
passage ID, document ID and BM25 score in rank order. Schema 2 indexes passage,
title and section text with English stemming, exact role and document keys;
schema 1 can still open for passage-only search but cannot supply an article filter.
Default title/section weights are 4.0/0.5. Token deduplication, title/section stopword
exclusion, minimum word-match settings, role/article restrictions and configurable
inner-title weight are observable behavior. Pack-level indexes cover SQLite shards.

This first-party Rust bridge must be replaced if that supported profile is to
meet the C++ requirement. SQLite FTS is not assumed to reproduce ranking. Preserve
fixture indexes/expected rankings, compare a native backend and stage reversible
index rebuilding; retain old indexes for rollback. That work remains M6.

## Baseline preservation and outstanding measurement

The pre-port archive/manifest includes untracked upgrade code and carried edits.
SQLite's native backup API (through the sqlite3 CLI) copied upgrade fact/demo
databases into `.state/cpp-migration/baseline/databases/`; no original sibling or
live installed data was touched. Existing independent-request reports were copied
before rerunning their reference workflow. New generation uses
`.state/cpp-migration/reference-artifacts`.

The 12 original CTest suites passed. The fresh independent twelve-output workflow
is tracked in `baseline/independent-requests.log`; do not infer completion from the
historical reports. The 32-case interpretation comparison, complete performance
replays and no-Python install/profile matrix remain separate open checks. The two
sealed evaluation files remain uninspected.
