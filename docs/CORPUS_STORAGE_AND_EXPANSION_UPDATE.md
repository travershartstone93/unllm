# unllm — Corpus Storage and Programming-Knowledge Expansion

**Status:** Proposed roadmap update / implementation specification  
**Version:** 1.1 — 2026-11-18  
**Applies to:** `UPGRADE_PLAN.md`, `CPP_RUNTIME_MIGRATION_PLAN.md`, `SYMBOLIC_INTERPRETATION_PLAN.md`, `CODING_CAPABILITY_ROADMAP.md`, and `FUTURE_CODING_CAPABILITY_ROADMAP.md`  
**Recommended repository location:** `docs/CORPUS_STORAGE_AND_EXPANSION_UPDATE.md`

### Deployment on this host, 2026-11-18

The Timetec USB volume now holds the installed corpus and its indexes. All 587 files matched the internal SHA-256 manifest after remount. The default shell command and the upgrade launchers use the external volume. The internal corpus is available for rollback. Future pack sources and build staging have dedicated directories on the USB drive. Initial programming sources have been acquired. Passage selection, vectorization and the native configuration work in S2 remain open. [Migration evidence, paths and rollback](../experiments/corpus_storage/s0/MIGRATION.md)

## 1. Purpose

This update records the agreed direction for unllm's knowledge corpus, physical storage, programming dataset expansion, provenance, and the corpus-to-code construction interface.

It is **not** a replacement for the foundational or future coding roadmaps. It adds the data-management work needed to support them. Its purpose is to make unllm's existing corpus portable and to prepare high-quality programming knowledge for later research.

**Newly agreed architecture:** Use a **hybrid capability-resolution pathway**: invoke an available approved library operation; reuse eligible retrieved implementations; adapt a near match; or synthesize an implementation when necessary. Requirements and explicit contracts govern all choices. **Still open to experiments:** candidate ranking, how deeply to lower source to typed IR versus treating it as an opaque callable, which extracted contracts can be trusted, and whether small self-developed ML improves any step. Do not confuse agreeing on the pathway with demonstrating that it works.

## 2. Established decisions and constraints

1. **Preserve the existing corpus.** The installed knowledge packs are approximately **65 GB in total**, principally English Wikipedia. They continue to support unllm's secondary historical, cultural, and reference-answering purpose.
2. **Move the entire existing *knowledge corpus* to a 256 GB external drive.** This includes installed reference-pack databases, their search indexes, and relevant corpus manifests/metadata. It does not automatically include the executable, source tree, compiler toolchains, reranker model weights, or caches unrelated to corpus storage.
3. **Keep the engine and active development tools on the internal drive.** The user has approximately **100 GB free internally**, but that space should not be treated as a requirement for permanent corpus storage.
4. **Expand programming coverage deliberately**, initially prioritizing C++ and Python, with complete GUI examples, tests, build files, and algorithm descriptions where useful.
5. **No mandatory internet or model service at runtime.** Knowledge packs are local. Internet-connected acquisition, including Codex/Claude Code as development teachers, is permitted when approved, but any needed shipped artifacts must work independently afterwards.
6. **The unllm host implementation is to be C++20.** Existing Python ingestion/migration tooling may be used as temporary compatibility infrastructure during the ongoing native migration, but new permanent corpus-management responsibilities should target native C++.
7. **Optional, versioned knowledge packages** are the intended distribution model. No fixed maximum package size, but individual packages should be selectable and independently removable.
8. **No silent data loss, destructive moves, or automatic downloads.** Downloads/installations require approval. Migration is copy–verify–switch–observe–retire, never move-and-hope.
9. **The target host is an ordinary CPU-first PC**, approximately 16 GB RAM and a 3 GHz-class processor, optional GPU. Disk-backed corpus operations must use bounded memory.
10. **Windows and Linux are future targets** for generated applications; data-root configuration should remain platform-neutral even if the first storage migration is executed on Linux.
11. **Hybrid corpus-to-code construction is the chosen direction.** Reuse existing eligible components, adapt them when appropriate, and synthesize new code for unmet requirements; never turn retrieved text directly into trusted code.
12. **Retain the original requirements as authority.** Retrieved material, neural proposals, and approximate matches cannot silently weaken requirements, authorize external actions, or establish correctness.

### 2.1 Repository observations, subject to current-code confirmation

The public `docs/PACKS.md` documents:

- Current installed packs: `~/.local/share/unllm/packs/`.
- Source downloads: `~/Downloads/unllm-knowledge/`.
- Pack structure: `manifest.json` and `shard-NNN.sqlite` files with text, FTS5 indexes, and source metadata.
- A separate reranker model directory: `~/.local/share/unllm/models/reranker/`.
- Separate progress files and runtime resources.

These are **documented paths, not a guarantee that every currently installed file still uses them**. The implementation agent must inspect the working branch and local filesystem before making changes. In particular, locate any Tantivy indexes, vector-search databases, caches, symlinks, hard-coded absolute paths, or new native ingestion paths introduced after the documentation was written.

**Baseline reference:** [`docs/PACKS.md`](https://github.com/travershartstone93/unllm/blob/master/docs/PACKS.md).

## 3. Desired storage topology

Illustrative Linux layout (the actual mount point must be selected after drive inspection):

```text
Internal drive
  unllm/                       # C++ source, executable, project/build files
  ~/.config/unllm/             # User settings; external data-root configuration
  ~/.cache/unllm/              # Small bounded ephemeral caches if appropriate
  ~/.local/share/unllm/models/ # Model assets unless explicitly migrated later

External drive (stable mount, e.g. /mnt/unllm-data/)
  packs/                       # Installed Wikipedia, Stack Exchange, etc. packs
  programming-packs/           # Optional programming packs, if distinct
  source-manifests/            # Provenance, licence and inventory records
  capability-index/            # Optional derived programming metadata / contracts
  verified-knowledge/          # Versioned approved reusable components, if distinct
  staging/                     # Temporary corpus downloads / incremental import
  experiments/                 # Test datasets and measured corpus comparisons
  .unllm-volume-id             # Explicit marker identifying the expected volume
```

The directory layout is **proposed**, not an assertion of existing code support. Preserve existing pack schema and on-disk conventions unless a versioned migration is explicitly justified. In particular, the legacy `packs/NAME/manifest.json` and SQLite shard structure should remain readable.

### 3.1 A configurable data root is required

Introduce one canonical, configurable **corpus data root** for all knowledge-pack reads, pack listings, installation/removal, and index discovery. A proposed interface is:

```text
--data-root PATH            # Optional CLI override, if suitable for command design
UNLLM_DATA_ROOT=PATH        # Proposed environment variable; NOT assumed to exist today
config: data_root = PATH    # Persistent setting
legacy default             # Existing ~/.local/share/unllm location
```

Define and test one documented precedence order (for example, CLI > environment > config > legacy default). Avoid independent, conflicting path rules in ingestion, search, resident-server startup, and maintenance commands.

A Linux symlink from the old `packs` directory to the external volume may be a **temporary compatibility bridge**, but should not be the only supported long-term design. A persistent, explicit data-root setting is preferable, especially for future Windows support and portable installs.

If the configured root is unavailable, **fail safely with a specific diagnostic**. Never silently create a new empty pack library at the legacy location, rebuild packs without approval, or switch to a different corpus without telling the user.

### 3.2 Separate the corpus from models and execution state

The word vectors embedded in or shipped with the program, the separate reranker weights, any optional reader models, generated programs, and code-builder toolchains are not automatically part of this relocation. Audit them individually and keep their paths separately configurable. The target is to move **the entire corpus and corpus-specific indexes**, not every byte associated with unllm.

## 4. Capacity plan for the 256 GB external drive

Drive labels quote decimal GB; OS tools may display GiB. Verify **actual free bytes after formatting**. Do not plan against the full advertised capacity.

| Use | Initial planning allocation | Notes |
|---|---:|---|
| Existing reference packs | ~65 GB | Actual measured size is authoritative |
| New programming packs and their persistent indexes | Up to ~75 GB | Expand only after usefulness measurements |
| Download/ingestion staging and experiments | Up to ~35 GB | Prefer streaming and per-shard processing |
| Reserve / filesystem headroom | **At least ~40 GB** | Include replacement-pack and SQLite growth needs |
| Unallocated flexibility | ~41 GB nominal | May be lower after formatting and overhead |

This is a **budget, not a commitment to consume it**. An agent must re-evaluate it using `df`, filesystem metadata, and actual SQLite/FTS index amplification. Avoid running builds if they would violate a configured minimum-free-space guard.

The previously discussed C++ and Python third-party Stack v2 parquet subsets were estimated at approximately **41.1 GB and 34.5 GB compressed**, respectively (roughly 75.6 GB combined). Those historical figures must be reverified before use. **Do not attempt to retain both entire compressed datasets, their expanded forms, and finished search indexes at once** on a 256 GB drive.

Treat compressed source size, extracted source size, parsed records, SQLite/FTS indexes, structural metadata, and temporary build copies as different storage categories. No assumption of 1:1 sizes is allowed.

For large source data: process shard-by-shard, write resumable checkpoints, verify each completed shard, and discard approved intermediate data when retention is unnecessary. Raw source retained for auditing must still meet licensing and storage limits.

## 5. Migration procedure — reversible and independently verifiable

### Phase S0 — Inventory and freeze the baseline

Before writing or copying:

- Determine the external drive's device, capacity, type (SSD/HDD/flash), filesystem, transport speed, mount state, health, and current contents. **Do not reformat an occupied drive without explicit approval and a separate backup.**
- Identify all knowledge-pack and index paths actually used by the runtime, ingestion tools, resident server, tests, and configuration.
- Record installed pack names, versions, shard counts, sizes, manifest metadata, and source identifiers.
- Inventory SQLite databases, their `-wal`/`-shm` sidecars if present, Tantivy/vector index directories, and any open handles.
- Capture a frozen set of representative questions and baseline answers, citations, search timings, and errors across Wikipedia and Stack Exchange.
- Capture the current installed-package and toolchain state without deleting or rebuilding anything.
- Determine whether some original downloaded source files need to remain accessible for future pack rebuilds; distinguish them from **installed** corpus files.

**Output:** `corpus-inventory.json`, pre-migration performance/functional report, and exact list of directories to relocate. Avoid requiring a precise preselected external mount path.

### Phase S1 — Prepare stable storage

- Prefer a Linux-native filesystem such as ext4 for a Linux-only host corpus, subject to actual access needs. If direct Windows mounting is required, compare alternatives explicitly before choosing; filesystem semantics, >4 GB files, locking, permissions, and durability matter for SQLite.
- Establish a stable mount mechanism by UUID or equivalent, rather than relying on removable-drive names like `/dev/sdb1`.
- Create a persistent volume identifier/marker and verify that the correct external volume is mounted before searches or writes.
- Check permissions, directory traversal, SQLite database access, atomic rename capability, free space, and expected disconnect behavior.
- Do not move application executables or install a model dependency as part of this stage.

**Output:** A mount and data-root plan that is safe across reboot, reconnection, and the chosen user's normal login process.

### Phase S2 — Make paths configurable (native target)

- Centralize corpus path resolution in C++20; expose it consistently to runtime search, pack management, index discovery, status tools, and later ingestion.
- Retain read compatibility with the existing pack schema and names.
- Ensure background/resident processes use the same resolved root, with clear diagnostics on configuration mismatch.
- Use paths relative to the corpus root for newly created manifests and indexes wherever possible; preserve original source URLs, licence metadata, and externally meaningful IDs.
- Include tests for paths with spaces, Unicode, missing volumes, read-only volumes, and incorrect volume identity.

**Gate:** All pack-reading paths can point at a test corpus outside the default home directory, without copying test files back to the legacy path.

### Phase S3 — Copy without deleting the original

- Stop pack builders, pack-writing processes, and resident services; ensure SQLite snapshots are consistent. Checkpoint WAL files or use a consistent backup method as appropriate for the database's current journal mode.
- Copy the **installed corpus and its actual corpus indexes** to the prepared external data root. Preserve structure, timestamps, permissions where supported, and metadata.
- Verify file counts, byte counts, manifests, and content hashes or an equivalent read-back comparison; verify SQLite integrity on a representative or complete set as justified by build cost and risk.
- Keep the original internal corpus intact. No `mv` as the first migration operation; no deletion during copying.
- Record a migration manifest and log all exceptions.

**Gate:** A complete verified external copy exists before unllm is pointed at it.

### Phase S4 — Switch, exercise, and observe

- Change the canonical corpus root to the external location.
- Run pack listing, manifest inspection, representative question answering, code-passage lookup, source citations, and full-text search across more than one pack.
- Check both cold-start and resident-server behavior.
- Record response time, peak RAM, I/O utilization, and any impact of the external device, including p50/p95 search latency.
- Simulate a missing/disconnected drive while unllm is idle and while a query is initiated. Require a controlled error, not a crash, hidden fallback, or automatic download.
- Test pack addition/removal and interrupted incremental build against a **nonproduction test pack** on the external volume before enabling production writes.

**Gate:** Correctness is unchanged; performance differences are measured and acceptable; interrupted/unavailable storage fails safely.

### Phase S5 — Rollback and retirement

- If validation fails, restore the previous data-root setting and resume using the original intact internal corpus.
- Retain the original corpus through a defined observation period and at least one reboot/reconnect test.
- Delete old copies **only after explicit user approval**, after the rollback window and independent verification. Before deletion, confirm no other programs rely on those files.
- Reclaim the internal disk space only after the external pack inventory and database-integrity report are stored.

**Gate:** The user can explain and execute rollback without reconstructing 65 GB of downloaded data.

## 6. Programming-corpus expansion

The purpose of programming data is to broaden unllm's *available knowledge* and supply candidate implementations and evidence to the agreed hybrid construction pipeline. **The mechanism for reliably extracting and validating computational meaning remains experimental.** Existing encyclopedia and cultural packs must not be discarded to make room.

### 6.1 Initial language scope

- **C++**, with attention to standard-library usage, C++20, headers, types, algorithms, build structure, tests, and GUI examples.
- **Python**, especially independently testable algorithms, standard-library usage, data processing, and corresponding examples useful for multi-language output.
- Build/support files such as CMake and project manifests may be retained as **supporting context**, without introducing a third primary programming-language corpus.

### 6.2 Sources to evaluate (not automatic inclusion)

| Source | Likely contribution | Acquisition decision |
|---|---|---|
| Existing Stack Exchange packs | Explanations, troubleshooting, snippets | Preserve; audit programming-site coverage |
| cppreference | C++ language/API reference | Evaluate downloadability and redistribution terms |
| CP-Algorithms | Algorithms, explanations, C++ examples | Curated inclusion candidate |
| Exercism C++ and Python | Requirements, example implementations, tests | High-value pilot candidate |
| Qt and other desktop GUI example projects | Events, windows, charts, UI integration | Curate; check per-file licence and buildability |
| Selected permissively licensed open-source projects | Multi-file structure, tests, CMake and real usage | Pilot after provenance tooling |
| The Stack v2, filtered to C++ and Python | Wide variety of real code | **Selective/streaming experiment**, not wholesale import |
| CodeNet / CodeSearchNet | Broader algorithms and text-to-code pairs | Defer until needed by measured experiments |

The Stack is **supplementary**, not a replacement for documentation, tested exercises, API reference material, or existing Stack Exchange packs. Its size alone is not evidence of value.

On 2026-11-18 the first programming download acquired pinned snapshots of Exercism C++, Exercism Python and CP-Algorithms, plus nine selected implementation, test, README and license files from optional-lite and more-itertools. The USB data root holds the downloads in `sources/programming-pilot`. They contain 3,969 regular files with 3,344 distinct content hashes across the five sources. The three archives have archive checksums. All five sources have original paths, per-file hashes and acquisition records. These raw files await selection and passage construction before vectorization. The [acquisition manifest](../experiments/corpus_storage/programming-pilot.json) records exact revisions and root license headings.

### 6.3 Corpus acceptance and provenance

Each imported collection must record at minimum:

- Dataset name, upstream project, revision/date, acquisition URL, retrieval time.
- Licence, attribution, applicable restrictions, and redistribution decision.
- Original file/repository path, commit or source revision where available.
- Language and content class (reference, example, implementation, test, build file, question/answer).
- Content hashes, deduplication rules, and original-to-indexed-record mapping.
- Parsing/index version and validation outcomes.
- For programming entries where extraction is supported: symbol identity, signatures, types, dependencies, applicable target-language/platform constraints, known effects, source-to-record links, available tests, and contract evidence. Preserve unknown values as unknown; do not invent them.

Do not assume a GitHub repository's top-level licence automatically licenses every embedded third-party file. Reject or quarantine records with unsupported redistribution status from **distributed** knowledge packages.

Inspect and filter obvious generated/vendor files, duplicates, empty or malformed files, embedded credentials, and known unsuitable examples. Maintain a reversible record of filtering decisions.

A file that parses, compiles, or has passing tests is **not automatically a verified, generally reusable algorithm**. Distinguish source credibility, buildability, test coverage, and formally checked properties.

### 6.4 Keep packages modular

Suggested independent pack domains:

```text
reference-general       # existing encyclopedia, dictionary, cultural knowledge
reference-programming   # language and API documentation
algorithms-examples     # explanations, examples, associated tests
realworld-code-cpp      # curated C++ code with provenance
realworld-code-python   # curated Python code with provenance
gui-examples            # curated desktop GUI programs and references
verified-capabilities   # optional accepted functions/contracts/adapters and evidence
```

These are **logical labels**. Reuse the existing versioned-pack implementation rather than inventing an incompatible format unless experiments justify it.

The user should be able to install, replace, inspect, and remove each optional programming pack separately, with understandable storage costs and licence details. Packages should remain usable offline after initial acquisition.

### 6.5 Hybrid corpus-to-code interface (chosen architecture; implementation subject to evaluation)

The corpus is an **evidence and candidate source**, not a code-emission authority. The programming engine must resolve each required capability independently through four possible paths:

1. **Invoke:** Use an already approved standard-library, runtime, framework, or local capability through its documented interface. Prefer this to copying equivalent source where requirements and constraints are met.
2. **Reuse:** Incorporate a retrieved implementation only when its provenance, licence, dependencies, target environment, interfaces, and required behavior meet the acceptance policy.
3. **Adapt:** Construct a controlled modification of a near-matching implementation using typed IR or an AST-aware transformation; check the resulting behavior as a new candidate.
4. **Synthesize:** Construct an implementation from general-purpose primitives and contracts when suitable components are absent, ineligible, or more expensive to adapt than to synthesize.

These are **per-obligation alternatives**, not one strategy for the entire application. A generated desktop program may invoke a standard library, reuse a tested reader, adapt a chart widget, and synthesize novel application-specific state transitions. The initial deterministic search policy can normally consider invoke/reuse before adapt/synthesize, but it must be able to select synthesis early when retrieval is demonstrably unsuitable. Ranking, parallel candidate exploration, and learned search guidance require benchmark evidence before adoption.

#### A. Typed requirement and evidence boundaries

The native C++20 planner must give each required capability a stable ID and explicit, inspectable obligations: input/output types, observable behavior, preconditions, relevant postconditions, effects, platform/language constraints, dependencies, resource limits, and any user-imposed restrictions. An omitted or unprovable requirement must remain unresolved, not silently default to satisfied.

The corpus interface returns **candidate records plus evidence**. It cannot modify authoritative requirements or independently authorize file, network, hardware, or privileged operations. Requirements may change only through a user-approved clarification/revision path governed by the interpretation and future-coding roadmaps.

#### B. Capability records, source mapping, and validation states

Create a derived **CapabilityRecord** schema, linked by stable content IDs to original corpus entries rather than copying raw source indiscriminately. It should be able to represent:

```yaml
id: example:rolling-average:001
origin:
  pack: realworld-code-cpp
  repository: example/repo
  revision: pinned-revision
  path: src/rolling_average.cpp
  content_hash: sha256:...
  license: MIT
capability: rolling_average
language: cpp
platforms: [linux, windows]    # asserted only if established
signature:
  input: [sequence<float>, integer]
  output: sequence<float>
contract:
  preconditions: ["window > 0"]
  postconditions: []            # unknown until checked or specified
  effects: []
dependencies: [cxx-standard-library]
representation:
  mode: opaque_source           # or supported_typed_ir
  symbol: rolling_average
validation:
  parsed: true
  compiled: false
  tests: not_run
  formally_checked: false
```

This is a schema illustration, **not** a claim about an actual source file or its verified behavior. Store unknown fields explicitly. Preserve all source references needed to inspect, attribute, reproduce, or exclude the candidate.

Keep distinct evidence levels:

- **Extracted:** metadata/signatures/AST facts are parsed; behavior remains unverified.
- **Built:** the candidate compiles in a specified environment; this does not prove semantics.
- **Tested:** specified independent tests and edge cases pass; record their scope and environment.
- **Formally checked:** specific stated properties were proved under recorded assumptions; this is not unrestricted program correctness.
- **Eligible for reuse:** a separate **policy decision** incorporating the above evidence, licence compatibility, platform support, dependency approvals, side effects, and risk. The eligibility flag must not be inferred merely from `tested: true`.

Use a typed facade or interface contract for a complex retrieved implementation when lowering all internals into unllm's IR is unnecessary. For simple supported functions, test conversion into native typed algorithm IR to enable direct structural synthesis and editing. The **opaque-versus-IR threshold remains an experiment**, and a black-box contract must not pretend that opaque behavior has been proved.

#### C. Candidate eligibility and route selection

For each requirement:

1. Retrieve approved capability records, library/API specifications, algorithms, and relevant implementation examples from the **programming** packs, using source-linked IDs.
2. Apply hard gates: requested output language/toolchain, supported target OS, interface/type compatibility, required side-effect permissions, available approved dependencies, provenance, redistribution/code-incorporation rights, and required behavioral evidence.
3. Reject, quarantine, or label as **reference-only** candidates that fail gates or have insufficient evidence. Reference-only examples may inform later synthesis but cannot be silently embedded in generated programs.
4. For surviving candidates, compare invoke, reuse, adapt, and synthesis alternatives using an inspectable deterministic baseline (estimated changes, dependencies, compatibility, validation effort, execution cost, and search effort). Do not convert a similarity score into a correctness proof.
5. Try bounded candidate construction. Validate individual obligations and the **integrated** application using independent checks. If a candidate fails, record the diagnostic and either attempt a targeted adaptation/repair or switch routes.
6. Commit accepted components transactionally to the generated project, along with origin, licence, attribution obligations, accepted tests/assumptions, and required dependency information.
7. Return **unsupported / blocked / incomplete / exhausted** when the available evidence and budget are insufficient; never claim universal correctness from a build or finite test suite.

A retrieved component may be preferable to new synthesis if it satisfies the same contract with fewer dependencies and better validation evidence. A small new synthesized helper may be preferable to a nearly matching library with many dependencies. These preferences are optimization criteria, not fixed categorical rules.

For cross-language tasks, source in another language may serve as a *reference or translation candidate*, but direct reuse requires an explicit supported runtime or translation path. Python source must not become a hidden Python dependency of unllm's C++20 host.

#### D. Structural adaptation and recursive knowledge retrieval

Adaptation must operate on supported typed IR or a source-language AST/symbol-aware edit representation, **not** regex patching of arbitrary code. Keep the source, candidate patch, required tests, and provenance trace; preserve unmodified requirements and roll back unsuccessful edits.

The construction/repair engine may perform a **new, narrower corpus query** when a concrete unresolved obligation or counterexample reveals missing information. For example, a rolling-history implementation may fail a bounded-memory requirement, prompting a query for fixed-capacity circular buffers. Record why each re-query occurred; cap queries per obligation and deduplicate previous search states so the system cannot report indefinite retrieval as progress.

Retrieved code and downloaded build scripts are untrusted. Never execute arbitrary corpus source outside a suitably isolated environment or with unapproved network, filesystem, credential, or hardware access. A successful compile or launch is separately reported from functional validation.

#### E. Learning and reusable-knowledge promotion

Log decisions and outcomes as source-linked, reproducible traces: requirement ID, candidate IDs, route selected, eligibility results, rejected reasons, edits, tested properties, resource costs, and final status. These traces may later train compact ranking/generative models or inform deterministic cost policies.

A successfully incorporated component does **not** automatically become a permanent trusted primitive. Promotion to an optional, versioned reusable-knowledge pack requires a separately reproducible contract, independent tests or scoped proofs, known provenance and licence, and held-out integration/regression checks. Packages must be disableable/rollbackable without rewriting the general reference corpus.

#### F. Implementation boundaries

- The runtime decision controller, capability schema, indexing interfaces, policy checks, and editing integration are native **C++20**.
- Keep existing reference QA independent and functional without optional programming packs.
- Do not invent code-specific facts when indexing source; unknown types, behavior, licence, or effects remain unknown until evaluated.
- Keep source-retrieval indexes separate from the compact verified-capability registry, even if they share a configurable data root.
- Do not hardcode application-specific recipes or embed benchmark answers as accepted corpus components.
- The **hybrid design is selected**, but its benefit over pure synthesis must still be established by controlled tests; a negative result should cause reevaluation rather than automatic expansion.

## 7. Initial experiments and decision gates

### Experiment D1 — Small programming-pack pilot

Assemble a **small curated sample**, not an enormous Stack import. Include examples from a structured source (such as Exercism), an algorithm/reference source, and a limited real-world C++/Python code sample.

Measure:

- Source bytes; derived pack and index bytes; peak staging space; ingestion time.
- Count of usable files, examples, associated tests, and documents by category.
- Deduplication rate, provenance completeness, licence coverage.
- Retrieval quality on questions about supported APIs and algorithms.
- Cold and warm search latency, peak memory, storage throughput.
- Correct behavior of the original reference QA path (regression protection).

**Pass condition:** New packs install, search, cite their sources, and can be removed cleanly without degrading general-reference pack functionality. The storage cost is measured and predictable.

### Experiment D2 — Incremental C++/Python source scaling

Try increasing curated source volume in controlled steps, **subject to free-space gates**. Suggested milestones can be approximately 5 GB, 15 GB, and 30 GB of *new retained source/index data*; these are study sizes, not required final package sizes.

At each step, keep the evaluation queries and parser/index version frozen. Measure diminishing returns, indexing amplification, and impact on the normal 16 GB RAM target.

**Stop condition:** If added corpus volume produces minimal benefit, excessive I/O latency, insufficient free space, or poor provenance/licensing coverage, do not scale the raw-code volume further.

### Experiment D3 — Hybrid corpus-to-code feasibility and ablation

A bounded arithmetic-fragment pilot is complete. Across 16 tasks, corpus-free extended search solved 11, and adding source-frequency rankings solved the same 11. Direct fragment reuse improved from 6 to 10 when two source shards were added. The broader D3 evaluation remains open. [Pilot results and replay](../experiments/corpus_ablation/README.md)

The **invoke/reuse/adapt/synthesize** design is selected at the policy level by section 6.5, but the effectiveness of its component extraction, validation, ranking, and IR/opaque representation must be measured before broad deployment.

Freeze a small source corpus containing known, licensed, independently testable C++ and Python functions plus relevant documentation. Freeze held-out programming requests, resource budgets, and independent test oracles. Exclude complete benchmark solutions, close duplicates, and derivative source from every evaluated retrieval/training split.

Run at least four configurations against comparable problems:

| Arm | Capabilities enabled |
|---|---|
| **A** | Existing symbolic synthesis with no programming-corpus access |
| **B** | Corpus retrieval plus eligible direct reuse/invocation |
| **C** | B plus structured extraction and typed/AST-aware adaptation |
| **D** | Full hybrid with targeted re-retrieval during synthesis and recursive repair |

For each, report functional tests, genuinely unseen requirements solved, build/launch outcomes, provenance and permission compliance, adaptation versus synthesis counts, time, search candidates, retrieval calls, peak RAM and disk use. Record which component or source record influenced each accepted solution; do not count a retrieved near-duplicate as structural generalization.

Then compare deterministic candidate ordering against **optional** lightweight ML ranking or typed-candidate generation on the same frozen evaluation. No ML or training corpus is required to deploy the deterministic baseline. Choose model scope, candidate budget, and opaque-source-versus-IR policy only from measured results.

**Pass condition:** At least one hybrid configuration reproducibly improves supported coding outcomes or reduces cost without increasing incorrect success claims, licensing violations, or unauthorized effects. A negative result is reportable and must block unjustified expansion, not block corpus storage migration or reference QA.

## 8. Operational safety and performance expectations

- All corpus operations must be CPU-first and memory-bounded; never require loading the entire SQLite collection or source dataset into RAM.
- External-drive disappearance must produce explicit error states. A read/write failure must not be reported as a successful search, install, or program build.
- Preserve atomic pack installation or equivalent consistency checks; no partial pack should become visible as complete.
- Index rebuilds must be resumable where practical, with free-space checks before each major stage.
- Support portability: volume-root selection must be independent of hard-coded user names or mount points.
- Keep the engine operable with **only the original corpus installed**; optional programming packages cannot become a mandatory dependency of general question answering.
- New network access, downloads, installations, or destructive changes require user approval.
- Preserve existing independently checked behavior and established C++ migration constraints.
- Package provenance must be available to users, including the ability to inspect the source behind a retrieved record.

## 9. Agent execution order and required artifacts

**Do these in order; do not start massive downloads before migration and measurement.**

| Order | Work | Deliverable |
|---|---|---|
| 1 | Repository + installed-disk inventory | `corpus-inventory.json`, path map, baseline report |
| 2 | Drive examination and storage configuration design | mount/data-root plan; no destructive actions |
| 3 | Implement/test configurable corpus root | native C++ path-resolution tests and documentation |
| 4 | Copy and verify existing 65 GB corpus | migration manifest, hash/consistency report |
| 5 | Switch, regression-test, and establish rollback | tested external root and recovery procedure |
| 6 | Pilot curated C++/Python knowledge packs | ingested pilot, licensing report, size/performance results |
| 7 | Evaluate scaling of real-world source imports | before/after retrieval and storage benchmarks |
| 8 | Implement a bounded hybrid capability-record pilot and evaluate D3 | inspectable invoke/reuse/adapt/synthesize decisions, ablation results, failure report |

For each milestone, the agent must report: what changed, exact paths touched, data copied/removed, validation performed, observed disk/RAM/time usage, known risks, and which approvals remain outstanding.

### Explicit prohibitions

- Do not delete the original packs immediately after a copy.
- Do not reformat or mount over an occupied external-drive directory without an explicit approval and backup plan.
- Do not assume the external drive always appears at the same `/dev/sdX` path.
- Do not configure an automatic fallback to a new, empty internal corpus after unplugging the drive.
- Do not install the entire C++ and Python Stack v2 subsets without a space and purpose review.
- Do not silently replace the existing index format or break references from corpus passages to their original sources.
- Do not make large neural models or internet calls required for accessing the corpus.
- Do not bypass capability eligibility, copy unverified retrieved source into generated applications, or represent finite tests as proofs.
- Do not expand the hybrid integration without passing the D3 evaluation gate; keep the approved four-route *policy* distinct from unproven extraction/ranking methods.

## 10. Final acceptance criteria

This update is considered implemented when:

1. The original approximately 65 GB reference corpus runs from the intended external volume, with the same pack inventory and verified data integrity.
2. Knowledge-pack location is configurable and consistently resolved across active search and management paths.
3. The program can detect a missing/unmounted drive safely and avoids hidden fallback or unintended rebuilding.
4. Normal historical/cultural question answering remains functional and sources remain inspectable.
5. The internal corpus copy has **not** been deleted without explicit approval and a rollback-ready verification period.
6. At least one small, versioned C++/Python programming knowledge pilot can be installed and queried with provenance and independent disk/performance measurements.
7. Packaging and ingestion respect available capacity and the minimum-free-space reserve.
8. The hybrid four-route corpus-to-code policy is documented with explicit evidence/eligibility rules, and a bounded pilot compares its usefulness against synthesis-only baselines. The choice of ranking, neural involvement, and opaque-versus-IR lowering remains evidence-driven.
9. Retrieved source used in a generated application carries inspectable provenance, licence and validation scope; untrusted/unlicensed candidates are not silently reused.

## 11. Open decisions to resolve during execution

- Actual external filesystem, drive type and health, cross-platform access requirements, and stable mount point.
- Exact current locations of every pack, Tantivy index, vector index, source download, and any auxiliary corpus metadata.
- Whether old downloaded raw source files need to be relocated as part of corpus management; they are distinct from the installed 65 GB packs.
- Whether any source files are large enough to require different staging/streaming strategies.
- Final programming-pack sizes and whether additional storage is warranted after pilot measurements.
- Which programming datasets can legally be redistributed as optional unllm packages.
- Thresholds and candidate-ranking policies for invoke, reuse, adapt, and synthesize; whether search branches run sequentially or concurrently.
- Which source classes should be represented as opaque typed callable components versus lowered fully into supported algorithm IR.
- How to infer reliable behavioral contracts from documentation, tests, or teacher-assisted proposals without mistaking hypotheses for proofs.
- Whether optional compact learned ranking or generation improves held-out performance over deterministic decisions.

---

**Roadmap relationship:** This is a corpus and storage update. It does not change the foundational native C++ migration sequence or declare the program-synthesis research solved. It establishes the hybrid **invoke/reuse/adapt/synthesize** interface as the intended direction while requiring independent experiments before claiming coding improvements, without sacrificing the established offline reference system.
