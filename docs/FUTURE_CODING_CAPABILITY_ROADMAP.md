# unllm — Future Coding Capability Roadmap

**Status:** Proposed post-foundation research and implementation plan  
**Prepared:** November 17, 2026  
**Primary implementation:** C++20  
**Initial deployment:** Windows and Linux; 16 GB RAM, approximately 3 GHz CPU; GPU optional  
**First target:** Autonomous construction of native-looking desktop applications  

> **Purpose:** Progress unllm from bounded, independently validated symbolic program synthesis toward autonomous construction, modification, and maintenance of useful multi-file software projects. An internet-connected AI teacher may assist research and knowledge acquisition during development, but normal unllm operation must be self-contained and must not call external AI services or require retraining. Small locally developed generative or non-generative models are eligible for controlled experiments, not assumed prerequisites.

This is a **future capability roadmap**, not a replacement for the existing implementation plans and not evidence that the proposed abilities already exist. The first task remains completing the existing C++ migration and foundational coding milestones.

## 1. Relationship to the foundational roadmap

The following repository documents remain authoritative for current work:

- [`UPGRADE_PLAN.md`](docs/UPGRADE_PLAN.md): master implementation roadmap, including stages 1–17.
- [`CPP_RUNTIME_MIGRATION_PLAN.md`](docs/CPP_RUNTIME_MIGRATION_PLAN.md): native C++ host migration, M0–M7 / stage 12.
- [`SYMBOLIC_INTERPRETATION_PLAN.md`](docs/SYMBOLIC_INTERPRETATION_PLAN.md): requirements grounding, roles, scope, ambiguity, and clarification.
- [`CODING_CAPABILITY_ROADMAP.md`](docs/CODING_CAPABILITY_ROADMAP.md): general algorithm IR, bounded synthesis, repair, acquired knowledge, and integrated workflows.

This roadmap assumes the proposed **recursive construction, structural editing, persistent program sessions, and autonomous repair** extension has been added to stage 15. If that extension has not yet been merged, it is a prerequisite, not something this document silently reimplements.

The future roadmap starts only when native C++ implementation and the foundational stage-14–17 interfaces have passed their respective gates. Design and small isolated feasibility probes may be performed earlier, but they must not disrupt or delay the port.

The future stages are numbered **18–24** to avoid renumbering the existing master plan. They are capability boundaries, not a promise that every stage is feasible or that all can be implemented in one release.

| Stage | Expansion | Core evidence | Dependency |
| --- | --- | --- | --- |
| **18** | Multi-toolkit desktop GUI construction | Fresh, launchable Windows/Linux-targeted applications including a live system-monitor dashboard | Completed native application/algorithm foundation |
| **19** | Multi-file project synthesis | Unfamiliar projects with several cooperating modules and repeatable builds | Stage 18 plus stable project IR |
| **20** | Autonomous abstraction discovery and experimental learning | Versioned acquired knowledge transfers to held-out tasks; learned guidance wins against symbolic baselines if adopted | Foundation knowledge-acquisition tools; proceeds alongside 19 |
| **21** | Existing-repository understanding and modification | Safe, requirement-preserving edits to unfamiliar repos | Persistent program sessions and 19 |
| **22** | Library, API, and hardware-interface grounding | New API integrations assembled from explicit contracts rather than whole-app recipes | 19–21 and permission model |
| **23** | Additional output languages | JavaScript/Rust (and later others) generated and verified without changing core synthesis | Stable language-neutral IR, tests, and packaging |
| **24** | Broader autonomous-project evaluation | Independent evidence across tasks, complexity levels, revisions, languages, and resource budgets | Representative results from prior stages |

Stage 20 can overlap stages 18–19 experimentally. A failed neural-learning experiment must not block the symbolic construction path. Stage 23 can also begin earlier with bounded pure functions, but its mature GUI/project claims require the corresponding representation and validation layers.

## 2. User-selected requirements and non-negotiable boundaries

### 2.1 Product target

The long-term target includes **all** of: individual functions, standalone applications, multi-file projects, and modifications to existing repositories. The first post-foundation emphasis is **desktop GUI applications**, followed by **multi-file applications and autonomous learning/abstraction discovery**. Existing-repository editing, API integrations, and additional output languages follow without being forgotten.

Initial desktop examples should emphasize:

- Live dashboards, charts, periodic timers, and observable state.
- File editors, converters, validation, and data-processing tools.
- Realistic system-monitoring operations, with deterministic simulated inputs for reproducible tests.
- Hardware interfaces later, without preselecting USB, BLE, serial, GPIO, or another protocol before measured feasibility work.

The user chooses the **output language for each project**. C++20 and Python are the existing supported targets; JavaScript, Rust, and others are later candidates. The unllm host remains C++20 regardless of target language. A Python interpreter is permitted only when needed to execute or validate requested Python output, not to implement the host.

### 2.2 Deployment and portability

- Minimum practical target: ordinary PC with **16 GB RAM** and a **roughly 3 GHz CPU**; no dedicated GPU or VRAM requirement. Record exact processor model and core count in benchmarks because clock speed alone does not predict synthesis performance.
- Run on **Linux and Windows**. Generated GUI applications should target both platforms where the selected toolkit supports them.
- If running on Linux and asked for Windows software, producing Windows-compatible source is acceptable. **Do not claim Windows execution or GUI validation until tests actually run on Windows.** Cross-compilation is optional, not a release prerequisite.
- Runtime must work **offline by default**. Downloads may be requested only with user approval.
- No required subscription, remote model inference, online teacher connection, or local training phase for recipients of a release.
- The core repository may be supplemented with **optional, versioned model and programming-knowledge packages**. There is no fixed upper disk-size cap for optional packages, but load time, memory use, license, provenance, and disk footprint must be measured and disclosed. A minimal functioning core cannot silently depend on an optional package.

### 2.3 Model policy: controlled experimentation

The constraint is **not** a ban on all generative machine learning. The concerns are unpredictable third-party local LLM behavior, loss of control over output, poor observed coding value, and excessive runtime hardware requirements.

- **Codex and Claude Code may be used as internet-connected development-time teachers**, including to propose datasets, rules, abstractions, training objectives, and candidate architectures.
- Normal runtime makes **no calls to those teachers** and does not require their availability.
- A **small model developed, trained, inspected, and distributed by the project** may operate at runtime, including generative use, if it meets CPU/RAM constraints and its output is checked through the same typed contracts and independent validators.
- Do not decide in advance whether a neural component may rank choices, propose typed IR fragments, or generate larger candidates. Test progressively more influential roles against a frozen symbolic baseline and retain only measured improvements.
- Model output is a **candidate**, never an authoritative premise, validated program, or permission to bypass the symbolic engine.
- Teacher-assisted acquisition, local model training, symbolic rule distillation, and runtime learning are distinct activities. Calling the teacher during development is online; the **distributed runtime** is self-contained.

### 2.4 Autonomy and authorization

Unllm may autonomously plan, generate, compile, test, diagnose, revise, and package software **inside an isolated, designated project workspace**, unattended for several hours if the user chooses. It must request approval before:

1. Downloading or installing dependencies.
2. Modifying any files outside its designated workspace.
3. Running generated programs outside the isolated test environment.
4. Publishing, uploading, deploying, or externally distributing a result.

Ordinary authorization to construct an application does not authorize those actions. Denied approval must yield an honest paused, blocked, or partial outcome, not a workaround. Network access within the test sandbox is disabled by default. Treat externally provided repositories and generated build scripts as untrusted code.

### 2.5 Completion terminology

The user's minimum **deliverable-completion criterion** for a desktop application is: **it builds/packages and launches on the tested target operating system**. This is distinct from a claim that it implements every requested behavior.

Every job and benchmark must therefore record separate states:

| State | Required evidence |
| --- | --- |
| `constructed` | Typed specification and generated source/artifact exist. |
| `built` | Toolchain/build succeeds in the declared environment. |
| `launched` | Packaged application starts without immediate failure on the actually tested OS. |
| `behavior_validated` | Independent functional/property and relevant GUI interaction checks pass. |
| `project_accepted` | User accepts the result or a separately authorized acceptance process completes. |
| `incomplete / blocked / exhausted` | Exact unfulfilled obligations and reason are reported. |

An application may be **delivered as launchable** without being declared behaviorally correct. For claims of increased synthesis capability, the **independent benchmark must still test behavior**; mere launching cannot count as algorithmic success. Do not invent unexecuted Windows tests, missing test results, or unconditional correctness proofs.

## 3. Architecture to preserve through every stage

The planned chain remains:

`request + repository context -> typed requirements -> program/project IR -> decomposition/search -> candidate -> target backend -> isolated execution -> diagnostics -> structural revision -> verification -> versioned artifact`

Architectural extensions should keep these boundaries:

1. **Meaning is not source code.** Preserve requirements, ambiguity, evidence, effects, and user choices separately from emitted syntax.
2. **Program IR is language-neutral.** Typed algorithm nodes own semantics; backends lower them to target syntax and toolchains.
3. **Project IR extends rather than replaces algorithm IR.** Add files, modules, interfaces, resources, build graph, packaging, platform capabilities, and dependency contracts.
4. **GUI IR is toolkit-neutral.** A view/action/state representation may be lowered to different toolkit implementations; platform/toolkit-specific features must declare capability requirements or fail explicitly.
5. **Existing source is not automatically trusted IR.** External-repository editing requires a separate source-analysis/import and edit-preservation layer.
6. **Candidate scores are not correctness.** Statistical guidance changes proposal order; independent tests and contract checks decide acceptance.
7. **Validation is independent of generation.** Keep held-out oracles, native reference interpreters, target toolchains, and source/IR traces distinct.
8. **No family-specific answer tables.** New reusable operations or verified abstractions must demonstrate transfer across structurally different tasks.

All native host code, including future control loops, indexing, model inference adapters, acquisition metadata, packaging orchestration, and normal product commands, belongs in C++20. Declarative logic/grammar assets and separately packaged models are allowed; do not revive a required Python worker.

## 4. Stage 18 — Multi-toolkit desktop GUI construction

**Objective:** Construct unfamiliar native-looking Windows/Linux desktop applications from requirements using a common GUI/project representation, with **multiple GUI toolkits supported from the beginning** of this stage.

### 4.1 Feasibility and toolkit selection

The exact toolkit set was intentionally left undecided. Start with a measured feasibility comparison across candidate native desktop toolkits. Qt is already part of unllm's foundation; other candidates may be evaluated, but do not commit to one based only on familiarity.

Measure each candidate's: native Windows/Linux appearance, supported controls and charts, timer/event semantics, target-language availability, package footprint, licensing/dependency implications, automated input testing, headless/CI feasibility, and CPU/memory costs.

Select **at least two viable GUI backends** for the first meaningful stage-18 experiment. Both must consume the same toolkit-neutral GUI requirements and expose explicit differences when a feature is unavailable. Do not implement a second backend by cloning a complete task-specific dashboard source file.

### 4.2 Required representation extensions

Add or stabilize:

- Window and document lifetime, layout constraints, reusable views, dialogs, menus, and settings.
- Typed event streams, periodic timers, cancelable jobs, and event subscriptions.
- Reactive/derived state, chart series, sampling windows, units, and update frequency.
- Main-thread UI rules and bounded background work; explicit shutdown and resource release.
- File read/write operations, log persistence, errors, configuration, and save paths.
- Platform capabilities and toolkit capability negotiation.
- Build, launch, icon/resource, and package manifests in project IR.

Use general providers such as `periodic_sample`, `bounded_series`, `chart_view`, `state_transform`, `file_log`, and `window_layout`. Their contracts must be independent of the named benchmark application.

### 4.3 First independent flagship benchmark: system-monitor dashboard

Freeze a requirements contract for a **live desktop system-monitor application with charts and timers** before changing generators for the test. The benchmark should include:

1. A main window with two or more changing system metrics (e.g., CPU usage and memory usage) displayed with units.
2. At least one live time-series chart with a bounded history window.
3. Periodic sampling controlled by a timer; start/pause/resume, frequency adjustment, and clean shutdown.
4. A native-looking layout, labels, and basic error/unsupported-sensor behavior.
5. A save or export operation for collected samples.
6. A generated project build manifest and a package that launches on the tested OS.
7. A deterministic simulated metrics provider so tests do not depend on fluctuating host load or privileged hardware access.

Live system-sensor access should be a replaceable platform adapter to the same sampling contract; tests must distinguish simulated correctness from observed OS integration. User-selected target language governs emission. A Windows result generated on Linux is recorded as source-compatible until actually built/launched on Windows.

### 4.4 Generalization, not one dashboard

The benchmark must include fresh variants that reorder/rename metrics, change sample windows and update intervals, alter chart/layout composition, remove features, introduce errors, and request a follow-up revision. Include file-processing/editor tasks to prevent the controller from degenerating into a system-monitor template.

Use at least two GUI toolkits and both existing output languages when those combinations are declared supported. If a combination is not supported, report coverage honestly and prevent it from counting in that cell's success rate.

**Stage gate:** Demonstrate independently generated, packaged, and launched applications across both toolkit backends, with a separately measured subset passing deterministic functional and GUI-event checks. Freeze a minimum suite of 12 supported GUI contracts plus six incomplete/unsupported/ambiguous cases; set a pre-registered success threshold after feasibility calibration. All attempts remain in the denominator. No incorrect `behavior_validated` claims, no per-task whole-app templates, and no regression of existing GUI/algorithm paths.

## 5. Stage 19 — Multi-file project and architecture synthesis

**Priority:** Highest immediately after the stage-18 GUI slice, alongside stage-20 learning research.

**Objective:** Move from independently synthesized functions and application handlers to cooperating project components with explicit cross-file contracts.

### Work

1. Create a native **ProjectIR** with project identity, files, logical modules, exports/imports, function and state ownership, dependencies, build targets, resources, tests, configuration, and packaging metadata.
2. Maintain cross-module contract obligations, symbol/namespace references, and a typed dependency graph. Reject cycles or unsupported initialization order explicitly.
3. Construct architecture at two levels: high-level module/dependency planning, then bounded function/handler synthesis inside modules. Do not search arbitrary source lines for an entire project.
4. Add transactional edits across multiple files. A revision changes the smallest suitable project/algorithm IR nodes, regenerates affected modules, and preserves unaffected user files and contracts.
5. Support at least two related generated modules plus tests and build resources, then expand to larger projects only after measured correctness.
6. Keep toolchain calls sandboxed and report what actually compiled, launched, or passed tests. A user-selected output language selects its appropriate build strategy.
7. Add stable code identity and generated/manual-file boundaries. If a previously generated file was manually edited, detect divergence and ask how to preserve or import those changes; never silently overwrite them.

### Evaluation and gate

Freeze unfamiliar multi-file tasks spanning dashboards, file transformation, and stateful utilities. Each must require substantive interaction across files rather than arbitrarily splitting one function. Report module count, dependency graph, regenerated fraction after revision, compile/launch status, independent behavior tests, and resource use.

**Stage gate:** At least three distinct task families demonstrate multi-file construction and a changed requirement with preserved earlier behavior, including an end-to-end packaged launch on the tested OS. No hardcoded project-specific module graph, hidden complete implementation, or silent overwrite of manual changes.

## 6. Stage 20 — Autonomous abstraction discovery and experimental learning

**Priority:** Co-primary with multi-file construction. It may run as separate research experiments without changing the default engine.

**Objective:** Increase success and search efficiency on fresh tasks by acquiring **reusable, verifiable programming knowledge**, while investigating whether small self-developed models provide measurable value.

### 6.1 Versioned knowledge lifecycle

Represent every acquired package with:

- Unique ID, semantic version, license, provenance, and creation method.
- Supported IR/schema/toolchain versions and declared platform or toolkit capabilities.
- Typed interfaces, preconditions, postconditions, implementation or rule bodies, and effect declarations.
- Independent tests, proof obligations where applicable, counterexamples, and known limitations.
- Search/compile/runtime cost estimates and memory/disk footprint.
- Evidence of transfer to **new structurally distinct tasks**.
- Dependency and conflict information for other packages.

Use lifecycle states such as **proposed -> quarantined -> independently evaluated -> accepted locally -> optional published package**. A user may keep acquired knowledge local. Publishing or uploading a package is an external action requiring approval.

### 6.2 Knowledge sources

Evaluate separately:

1. **Validated construction traces:** Extract recurring typed IR structures, useful lemmas, failure patterns, and small helpers from unllm's own successful projects.
2. **Symbolic abstraction proposals:** Generalize repeated structures into parameterized functions or decomposition rules and check their contracts.
3. **Teacher-assisted acquisition:** Use internet-connected Codex or Claude Code during development to propose rules, examples, critiques, or acquisition datasets. Teacher output is untrusted until independently checked; teacher calls are never part of distributed normal execution.
4. **Locally trained models:** Train a small project-owned neural network on permitted, provenance-tracked material where an experiment justifies it. Ship validated weights in optional versioned packages; recipients do not retrain.

Do not treat a teacher-generated complete answer as evidence that unllm synthesized the benchmark solution. Separate teacher discovery/calibration/development data from sealed acceptance tasks. Preserve provenance and distribution rights for datasets and weights.

### 6.3 Model-role experiments

Do not presuppose a transformer is needed or choose its role by intuition. Use an **ablation matrix** with identical frozen primitives, tasks, tests, and compute budgets:

| Arm | Intervention | Question |
| --- | --- | --- |
| A | Deterministic symbolic search | Baseline success and resource cost. |
| B | Additional verified symbolic abstractions only | Does acquired knowledge transfer? |
| C | Small statistical ranking / probabilistic grammar | Does search ordering improve? |
| D | Small model proposes typed IR fragments / decomposition | Does bounded generation improve construction? |
| E (conditional) | Small model proposes larger program candidates, all independently validated | Is stronger generation worth its cost and failure rate? |

Proceed from C to D/E only if previous evidence warrants additional complexity. Preserve a fully symbolic fallback and compare **absolute pass@budget, false success rate, latency, memory, search expansions, out-of-distribution transfer, and package size**. Report compilation/validation cost separately from model cost.

Runtime inference must meet the minimum 16 GB CPU-only target; GPU acceleration is optional. Define bounded model context/input representations. Model outputs must pass type checking, contract checking, and independent executable tests; even a high-confidence score cannot certify a candidate.

### 6.4 Promotion and rollback

Each candidate helper/rule/model package must pass fresh unseen tasks and a regression suite before local admission. Require comparative evidence on at least three structurally distinct fresh tasks per reusable package, consistent with the foundational knowledge-acquisition plan. Retain rejection records and permit disabling or rolling back a package without rebuilding the entire product.

**Stage gate:** A repeatable controlled experiment shows either a genuine held-out improvement from acquired knowledge/model guidance or a documented negative result that justifies retaining the symbolic baseline. **A neural model is never a mandatory gate.** An accepted knowledge package must work without teacher connectivity, retraining, or undeclared downloads.

## 7. Stage 21 — Existing-repository understanding and modification

**Objective:** Accept an unfamiliar local project as input, determine its structure and behavior within a declared supported fragment, and make requested edits without destroying unrelated work.

### Work

- Create a **RepositoryModel** of files, build targets, symbols, imports, dependencies, tests, generated assets, and editable vs protected regions.
- Ground natural-language instructions and follow-up references to concrete source entities with evidence and ambiguity tracking.
- For supported languages, use proper parser/AST/symbol-index facilities rather than regex edits; preserve code formatting/comments outside changed regions whenever possible.
- Map a requested change to affected modules/contracts, propose a patch transaction, and preview its scope before touching protected content.
- Execute available builds/tests in a sandbox using task-relevant configured toolchains; missing dependencies produce a blocked status and an approval request if installation is needed.
- Track pre-existing uncommitted changes and preserve them. Never silently overwrite user edits, delete unrelated files, or broaden scope when requirements are unclear.
- Permit successive revisions, rollback to the previous valid project state, and checkpoint/resume of longer jobs.

Start with small, well-structured repositories and a defined language/build subset. Large arbitrary repositories, generated vendored trees, opaque build systems, and platform-only APIs are explicit later boundaries.

**Stage gate:** Fresh repos support at least three categories of changes (function behavior, module/interface change, and GUI/file workflow), with all original relevant tests preserved or explicit reasons for unrun checks. No unauthorized out-of-workspace edits, no silent loss of user changes, and no fabricated validation claims.

## 8. Stage 22 — External APIs, libraries, and hardware interfaces

**Objective:** Acquire enough *structured interface knowledge* to construct unfamiliar integrations without implementing each application or API-specific workflow by hand.

### Work

1. Define typed **library/API contracts** for functions, types, stateful resources, errors, permissions, thread restrictions, platform availability, and version compatibility.
2. Support importing from installed headers, interface definitions, metadata, and approved offline documentation packs within declared parsers. Plain-text documentation remains evidence, not executable truth.
3. Separate API selection from implementation. The planner may propose an operation only when its contract is compatible with the program requirements.
4. Add capability/effect policies for file access, OS monitoring, network access, hardware, and privileged operations.
5. Use deterministic simulators and stubs for early hardware/network experiments. Real-device trials require explicit supported hardware and permission boundaries; the project has **not yet chosen** its first physical protocol.
6. Treat unknown library behavior, ABI mismatch, unsupported platform, and absent documentation as honest limitations. Avoid hallucinated function names or signatures.
7. Distill genuinely reusable interface adapters into optional knowledge packages with independent integration checks.

**Stage gate:** Given a fresh but supported API contract, generate a working small integration without adding a dedicated whole-application recipe, validate against an independent simulator or permitted real service/device, and document every external dependency. Permission denials must not trigger covert downloads or access.

## 9. Stage 23 — More output languages without duplicating the engine

**Objective:** Add selected output languages as independent backends, not separate language-specific synthesis systems.

Start with bounded pure-function emitters and semantic equivalence checks; extend to multi-file projects and GUI output only after the relevant APIs and toolchains are supported. JavaScript and Rust are early candidates; the user retains final language choice per project.

Each backend supplies:

- Type/value/effect mappings, precedence and escaping, runtime helpers, and source formatting.
- Build/run/package commands appropriate to the target platform and requested language.
- Compilation or parsing diagnostics mapped back to typed IR nodes.
- Independent executable checks and a declared supported language fragment.
- Platform/runtime/ABI boundaries, including integer overflow, error semantics, Unicode, and ownership where applicable.

JavaScript's ordinary numeric representation must not silently violate the foundation's checked 64-bit integer contract; use an explicit mapping or narrow the supported domain. Rust requires an ownership/borrowing strategy compatible with the represented dataflow; start with owned values and bounded constructs rather than claiming general borrow inference.

**Stage gate:** On frozen algorithm families, a new backend matches independent reference semantics and previously supported languages, including negative cases. New backend registration must not require modifying the core search/planning logic. GUI/project coverage is reported separately, not inferred from pure-function success.

## 10. Stage 24 — General autonomous-project evaluation

**Objective:** Establish how far unllm has progressed toward historical GPT-3-era coding capability **on genuinely comparable and independently verified tasks**, without confusing source fluency, launchability, and functional correctness.

Use independently authored and frozen tasks with structural rather than superficial separation. Assess:

1. **Interpretation:** Does the request map to the intended specification without dropping clauses?
2. **Algorithm synthesis:** Can unfamiliar control/data flow be constructed from the supported primitives?
3. **Desktop construction:** Can it build/package/launch novel Windows/Linux-targeted GUIs and pass independent interactions?
4. **Multi-file composition:** Do cooperating modules satisfy cross-file contracts?
5. **Autonomous repair:** Can it locate and correct seeded failures without weakening requirements?
6. **Sequential revisions:** Does it preserve earlier requirements and manual changes?
7. **Acquired knowledge:** Do validated packages transfer to new task structures under fixed budgets?
8. **Existing-repository edits:** Are modifications localized, buildable, and regression-safe?
9. **Operational cost:** CPU time, memory peak, disk, compile time, search candidates, model cost, toolchain availability, and wall-clock completion.
10. **Truthfulness:** Are failures, timeouts, unsupported requests, and unexecuted platform checks reported accurately?

Historical GPT-3/other coding evaluations can be used as **contextual comparison** only after matching task distribution, available tools, prompting/clarifications, test visibility, resource budgets, and success definition. A mismatched score does not establish parity. Publish tasks attempted, exclusions decided *before* evaluation, and complete failure accounting.

**Stage gate:** Reproducible independent reports identify concrete areas of progress and current bottlenecks. Make no unconditional claim of general programming ability from a few benchmark applications or a padded source-line target.

## 11. Long-running autonomy: convergence, checkpoints, and termination

This is cross-cutting and begins with the earliest stage-18 construction jobs. The user permits unattended jobs lasting **several hours**, checkpointed and resumable, but explicitly identified a failure mode: the engine may keep making superficial changes forever without finishing.

### 11.1 The controller must not confuse activity with progress

Record a persistent **obligation ledger**: each original requirement has an ID, measurable acceptance evidence, active/withdrawn status, and remaining proof/test obligations. A candidate's new source text, additional search nodes, or another passing development test is **not by itself progress**.

Track separately:

- Number and importance of independently satisfied active requirements.
- Outstanding typed holes and unsatisfied cross-module contracts.
- Regressions introduced and repaired.
- Actual independent acceptance-test changes, including failures.
- New information obtained from counterexamples rather than repeated equivalent failures.
- Search diversity and unique IR/candidate hashes.
- Build/launch/behavior-validation milestones.

The objective is to converge to a **finite acceptance condition** for the current job. New subtasks must trace to an original or explicitly approved requirement; self-generated tasks cannot expand the goal indefinitely.

### 11.2 Bounded subjobs; resumable projects

Each solver call, synthesis branch, compilation, executable validation, and repair attempt has explicit resource and time limits. The parent project job may continue longer, but the limits must be measurable and configurable. After a failure/stall, the engine may change strategy within the authorized search space; it must not simply repeat the same candidate.

Persist checkpoints atomically at meaningful boundaries, including program/project IR, requirement ledger, accepted revision, candidate hashes, toolchain/knowledge versions, pending obligations, counters, and safe next action. A resumed run must invalidate stale cache/solver results when dependencies or packages change.

Provide clear **pause, resume, cancel, and inspect-progress** controls. Unattended mode may continue until stopped, but it retains watchdogs and per-subjob limits; it must return an exhausted/blocked state rather than spin in an unproductive cycle. User-configured hard project time or resource ceilings remain available.

### 11.3 Mandatory stagnation experiments

Before enabling multi-hour unattended operation by default, construct adversarial development cases for:

- Alternating between two equivalent IR revisions.
- Adding/removing features while breaking an earlier requirement.
- Endless decomposition into new trivial subgoals.
- Repeated compiler errors caused by a broken emitter.
- Chasing mutually inconsistent tests or requirements.
- A slowly increasing development-test score with no improvement on the frozen acceptance contract.
- Repeated model proposals that look different textually but are semantically equivalent.
- Resource exhaustion and lost/restarted workers.

Require cycle/stagnation recognition, bounded recovery, intact last-valid checkpoints, and an honest terminal or paused status. There is no general proof that every programming search will find a solution; **termination of an individual bounded attempt is mandatory, and eventual solution is not guaranteed**.

## 12. Evaluation policy shared by all stages

1. **Independent task splits.** Separate development, acquisition, calibration, and held-out acceptance by program/GUI/project structure, not merely variable names or paraphrases. Never tune on sealed failure details.
2. **Frozen baseline and budgets.** Every capability/model/knowledge experiment has an A/B comparison against an unchanged baseline with equal task access and resource limits.
3. **Language and toolkit matrix.** Report precisely which requested language, GUI toolkit, and OS were built, launched, functionally checked, or not tested. Never infer missing matrix cells.
4. **No disguised templates.** General primitives are legitimate; a complete solution to one named benchmark inserted as a reusable component is not evidence of synthesis.
5. **Independent validators.** Compile/test output and use deterministic reference behavior or fixtures; don't accept the same generated code as its own oracle.
6. **Unsuccessful work counts.** Report unsupported, ambiguous, impossible-under-contract, exhausted, blocked-for-approval, compilation-failed, and functionally-wrong cases in denominators.
7. **Resource and provenance accounting.** Record CPU/RAM/disk, inference/training distinction, compiler/solver time, knowledge package versions, approved external effects, and source hashes.
8. **Rollback and user changes.** A failed revision must preserve the last valid state and unrelated source changes, including across checkpoints.
9. **No silent network dependency.** A clean offline installation with the declared local packages must reproduce relevant behavior; approved downloads are separate measured workflows.
10. **Narrow factual claims.** A launched GUI, a validated finite test set, a solver proof over a restricted grammar, and verified general-purpose capability are not interchangeable outcomes.

## 13. First future-stage implementation sprint (after foundation)

**Goal:** Design a reviewable stage-18 vertical slice before attempting broad GUI synthesis or neural training.

1. Confirm stages 12–17 gates and record the exact native C++ baseline, supported output-language matrix, OS test hosts, and package tooling.
2. Freeze an independently specified system-monitor dashboard contract and a smaller set of structurally different GUI controls/file-processing cases. Keep acceptance cases separate from development fixtures.
3. Run a limited technical comparison of native-looking GUI toolkits; select two viable backends based on declared constraints, not application-specific source reuse.
4. Define the toolkit-neutral GUI/event/chart IR and packaging manifest, including explicit unsupported-capability reporting.
5. Construct a **manually specified** minimal chart-and-timer application through both backend paths to prove emission and launch fidelity. Do **not** count this as language-to-GUI synthesis.
6. Integrate deterministic simulated metrics, event-loop/timer control, chart updates, and clean shutdown; test the output on Linux and Windows separately when available.
7. Only then enable the existing symbolic planner to propose the corresponding GUI components from fresh specifications, with independent tests and traces.
8. Add checkpoint/resume and stagnation-case instrumentation before permitting long unattended construction jobs.
9. Publish the feasibility results, failures, packaging evidence, memory/CPU cost, unsupported combinations, and next smallest experiment. Do not start neural training merely because the frontend can display a chart.

**First-sprint exit criterion:** The native engine supports two separately implemented toolkit backends for a common manually specified bounded GUI contract, with reproducible package/launch evidence on the tested OS and independent timed-event checks. A subsequent frozen synthesis experiment must establish whether the *engine*, rather than a hardcoded demo, can construct the application from requirements.

## 14. Required agent handoff and stop rules

For each bounded work slice, the implementing agent reports:

```text
Stage / experiment:
Repository revision and dirty-worktree baseline:
Declared task and supported languages/toolkits/OS:
Requirements and independent acceptance conditions:
Architecture/IR/schema changes:
Files changed and third-party dependencies:
Teacher/model/knowledge artifacts, versions, and provenance:
Tests executed and exact outcomes:
Unrun validations and reason:
Build / launch / behavior-validated status by OS:
Candidate, solver, model, compile, and total time:
Peak RAM and optional package size:
Iterations, checkpoints, regressions, and convergence status:
External approvals requested or denied:
Known limitations, unsupported cases, and rollback position:
Next smallest experiment or decision:
```

The agent must stop and report when a required permission is denied, external effects exceed authorization, source ownership is ambiguous, a frozen benchmark is contaminated, correctness is falsely claimed, or the next action would introduce a materially new architectural dependency without approval.

**Final principle:** The future roadmap succeeds only when unllm demonstrates *transferable general programming capabilities on unfamiliar tasks*, not merely longer generated source, increasingly impressive templates, or more candidate iterations. All progress must remain compatible with a self-contained, CPU-capable C++20 host and independent, inspectable verification.


## 15. Coding-request ambiguity detection and interactive clarification
Scope: Cross-cutting capability for natural-language construction, recursive editing, and existing-repository modification. Coordinate with SYMBOLIC_INTERPRETATION_PLAN.md and the foundational requirement-grounding/clarification interfaces; do not create a competing interpreter. The host-side controller and persistent session state are C++20.

Unllm must identify when a coding request permits multiple materially different implementations or lacks information required to construct or modify the requested behavior safely. Rather than guessing, it must be able to ask a targeted clarification such as "Did you mean X or Y?" and resume the same construction session with the user's answer.
Detection and decision policy
Classify unresolved uncertainty at least as:
- Reference ambiguity: "Change that function" could refer to multiple symbols or earlier revisions.
- Behavior ambiguity: "Sort the records" lacks a key/order, or "remove duplicates" leaves equality undefined.
- Interface ambiguity: Unknown file format, output structure, input type, platform, or required GUI interaction.
- Constraint conflict: New instructions contradict an active requirement or protected earlier user edit.
- Environment/dependency uncertainty: A needed toolkit, device, API version, or platform capability is unavailable or unverified.
- Permission uncertainty: Completing the request would require a new download, out-of-workspace modification, unsandboxed execution, or publication.
Use the existing semantic/requirement graph and repository symbol identities to enumerate supported interpretations. Retrieval, deterministic rules, and optional compact ML may propose interpretations; none establishes user intent. Do not fabricate numerical confidence values unless calibrated by held-out tests.

Ask for clarification when the ambiguity materially changes observable behavior, application architecture, data loss risk, rights/permissions, or user-approved constraints. Continue autonomously when there is a well-defined, documented, reversible default that cannot change those material outcomes; record any assumptions and offer them for inspection. A genuine permission gate must request approval, not be converted into a routine guess or assumed default.
Clarification interaction
Each clarification must include:
1. The unresolved requirement/component and why a decision is needed.
2. At most two or three concrete, mutually distinguishable interpretations when possible, expressed in normal programming language (for example, "Did you mean case-sensitive duplicates or duplicates ignoring case?").
3. An Other / describe option; do not force the user to accept a suggested interpretation.
4. The effect of each option on the generated program where not obvious.
5. Stable question and session identifiers so the response applies to the correct suspended obligation.
Ask one focused question at a time by default. Batch only when several independent blockers genuinely prevent progress and a compact combined question clearly reduces user effort. Do not repeatedly ask the same question after a usable answer has been given. If the user declines to clarify, keep the affected obligation unresolved and continue only with independent authorized subgoals.
Session, safety, and resumption
Represent clarification as structured state, for example ClarificationRequest { id, session_id, requirement_ids, ambiguity_type, alternatives, free_text_allowed, material_impact, status }. Checkpoint the current typed specification, attempted interpretations, and last valid program before pausing. The user's response becomes an explicit, versioned requirement update, not an implicit rewrite of the original request. Resolve the relevant obligation, invalidate affected candidate plans, and resume bounded construction or targeted repair.

If a later revision supersedes an earlier answer, preserve both in the requirement history and explain which one is active. Never claim behavior_validated for an unresolved requirement. Clarification does not waive compilation, regression testing, sandboxing, source licensing, or the external-action approval policy.
Benchmark and acceptance gate
Freeze previously unseen requests and revisions containing genuine ambiguity, unambiguous equivalents, misleading but plausible suggestions, conflicting requirements, unknown dependencies, and multiple similarly named functions in an existing repository. Measure:
- Material ambiguities detected versus missed.
- Unnecessary clarification rate on unambiguous requests.
- Whether proposed alternatives cover the user's intended meaning without fabricating unsupported options.
- Whether answers are correctly bound to the suspended requirement and reused in later turns.
- Number of repeated or contradictory questions.
- Preservation of prior program behavior after clarification-driven revisions.
- Correct blocked/incomplete results when information or approval is withheld.
Gate: Demonstrate end-to-end "did you mean X or Y?" clarification, checkpoint/resume, and correct requirement revision on unfamiliar coding and repository-editing tasks, without excessive interruption of unambiguous tasks. The feature is not an excuse to shift ordinary algorithm selection back to the user; unllm remains responsible for choosing implementations that satisfy sufficiently clear requirements.
