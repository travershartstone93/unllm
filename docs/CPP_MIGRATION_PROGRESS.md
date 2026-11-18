# Native migration implementation evidence

Implementation started 2026-11-13 following authorization to execute the full
upgrade path. This is an incremental port, not a completed C++ product or a
claim of general algorithm synthesis. Stages 13–17 remain after migration.
Last implementation update: 2026-11-17.

## Isolation and baseline

Work is confined to `unllm-upgrade`. The existing product binary and Python
reference remain available; new host code uses a separate `native/CMakeLists.txt`
and `build-cpp`. No CLI cutover or installed-command replacement has happened.

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

## Checks completed

All eleven current native CTest suites passed on 2026-11-17 in 64.80 seconds.
The complete run is recorded in `.state/cpp-migration/native-tests.log`.

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

A fresh manual factual conversation inferred Mira's location through London and
England, listed both premises, withdrew support after correction and restored it
after undo. A fresh structured stream request composed filtering, decrement,
deduplication, sorting and statistics; the native command emitted a 71-line Python
program and validated it on 37 cases. Neither observation establishes general
algorithm synthesis or overall speedup.

Captured Python reference outputs are development fixtures only. New tests and
host modules are C++; the default native test graph does not launch Python.

## Open migration gates

The inventory and reference reruns are recorded, but the full migration gates
remain open. Unicode-version compatibility, exhaustive serialization vectors,
process hardening and no-Python installation still need evidence. Factual importers
and the facts command remain to port. English program grounding, application and
shared-behavior specifications/solvers/emitters, GUI validation, public maintenance,
search/profile migration and packaging remain incomplete. The new utility command
does not replace the production CLI or implement arbitrary programs.

The downloaded cvc5 SDK includes unused upstream Java/Python bindings; final
packaging must select only native runtime artifacts. System ICU/OpenSSL/SQLite
versions are observed rather than a completed reproducible deployment bundle.
Native source paths are currently checkout-relative; relocation is not tested.
No performance improvement or production parity is claimed from these small tests.

Reproduce current development checks with:

```sh
cmake -S native -B build-cpp -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cpp --parallel 2
ctest --test-dir build-cpp --output-on-failure --no-tests=error
```

Run optional Python-target checks explicitly (the test driver remains C++):

```sh
build-cpp/native_stream_emit_tests "$PWD" python /usr/bin/python3
```

The isolated native stream command accepts a typed JSON specification:

```sh
build-cpp/unllm_generate_stream "$PWD" "$PWD/build-cpp/unllm_logic_worker" \
  specification.json cpp "$PWD/.state/cpp-migration/generated"
```

For Python, choose `python` and supply `--python-target /usr/bin/python3` to
validate, or `--emit-only` to emit without an interpreter. The current sandbox
exposes `/usr` and the job; target toolchains outside `/usr` still need the later
packaging/runtime-bundle work. Emission without validation is reported as
`emitted`, never `validated`.
