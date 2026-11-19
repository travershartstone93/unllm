# Plan: improve symbolic request interpretation

Proposed on 2026-11-12, following the [local-model experiment](LOCAL_MODEL_EXPERIMENT.md).
Status on 2026-11-18. The [native migration](CPP_MIGRATION_PROGRESS.md) is complete and the [numeric pilot](../experiments/symbolic_distillation/numeric_change/README.md) reached its feasibility stop. Milestone 1 passed its [160-trial history comparison](../experiments/interpretation_diagnostics/README.md). Milestone 2 passed its development gate on the native GUI behavior path. The first bounded role-binding trial in milestone 3 is complete. Coordinated numerical roles remain open. Python module references below identify historical responsibilities. The host implementation uses C++.

The objective is to understand more ordinary descriptions of already supported
program behavior while preserving accurate operands, scope and targets. Work stays
in unllm-upgrade. ACE/ERG with native MRS handling, SWI-Prolog, Soufflé and the existing typed
construction/cvc5 backends remain the tools. The local-model experiment stays
isolated and optional. The original checkout and carried user edits are preserved.

For coding requests, the next task is the [English-to-program investigation](#english-to-program-investigation). It defines how to decide whether the failure lies in interpretation, the contract interface, or construction before selecting a parser. Lightweight ML is a candidate. A full generative interpreter remains a last resort.

## English-to-program investigation

**Status: broader evaluation complete; general English coding remains open.** The [integration report](../experiments/interpretation_diagnostics/n16/bridge.md) records six-task parity and independent execution. The adjusted semantic parser passed its narrow stream development gate and then produced zero additional programs on the [32-request frozen evaluation](../experiments/interpretation_diagnostics/n16/broad-results.md). It remains experimental. This investigation connects stages 13b and 15; the GUI milestones retain their own fixtures and criteria.

**Question.** Can the same frozen system turn unfamiliar English into working programs by composing operations it already knows? An improvement on synonyms alone would leave that question open. In N11, English scored 0/6 and construction from supplied types and examples scored 3/6. In N14, five of eight prompts had no ACE analysis, and no prompt produced code. N15 recovered three additional construct-label sets with retrieval but did not score arguments, scope, or code. These results justify testing more than one explanation. [Central experiment log](EXPERIMENT_LOG.md)

**Prefer established industry solutions.** After locating a failure, check how deployed systems solve that class of problem and which maintained implementations unllm could reuse. Record the best-fitting candidate, evidence of production use, what it actually supplies, and what adaptation it needs for local C++ execution and typed contracts. A research toolkit can provide an experimental baseline. Prefer an existing component that passes the frozen evaluation. Justify custom work with a specific uncovered requirement, and keep that work limited to the gap.

Group development failures by mechanism before changing the implementation. For example, misreading a quoted loop name and misreading a quoted function name may share one cause: losing the boundary between a literal and an instruction. Fix that boundary once and test transfer on other constructs. A failed request belongs in the regression suite; adding a regex, alias, or handler keyed to its wording does not establish a general fix. Assess the change on previously unseen requests.

### 1. Freeze what success means

- Use N11, N13, N14 and N15 for development and regression. Retain their original prompts, denominators and results. The previously discussed examples are development cases.
- Proposed coding evaluation: 24 positive requests, eight each for a requested code structure, a requested behavior, and a behavior with an explicit implementation constraint. Arrange them in pairs. Some pairs express the same meaning differently. Others differ in one consequential detail such as operand order, scope, a required loop, or an excluded effect. Include scalar calculations, sequence processing and nested control flow. Reserve at least half the positive requests for compositions absent from fitting and calibration, with the individual operations represented there. Include eight separate controls covering genuine ambiguity, contradiction and unavailable capabilities. Include valid quoted mentions among the positives to check that a quoted construct name does not trigger it.
- For each request, record the required inputs, outputs or effects, variable references, scope and order, explicit implementation constraints, permitted defaults, and any unresolved choice. Accept semantically equivalent contracts and alternative implementations. For a bare snippet, declare which structure must appear and which illustrative choices are free. For behavior, write independent expected results with inputs that distinguish the plausible wrong meanings. For clarification, specify the missing choice and the expected result after an answer.
- Freeze the dataset, training/development split, scoring rules, backend capability inventory, binary revision, and search/time budgets before comparing candidates. Keep acceptance annotations and execution inputs out of training, retrieval and candidate selection. Use independently authored or sourced requests where available and record access and provenance. Label a same-author trial as development validation. Collect the final confirmation set separately.

### 2. Locate the first failing stage

Run the following controls first on the public development cases. Keep the 32 fresh requests sealed during diagnosis and candidate selection. Once the intervention, training data and runtime budgets are frozen, repeat the paired comparisons on those fresh cases. Record the first failed stage and retain every case in the results.

| Input to the test | Evidence it provides |
| --- | --- |
| Original English through the current CLI, both languages | Trace parser readings, proposed meanings, selected requirements, emitted code, and independent execution. Record missing instrumentation as unknown. |
| Independently specified requirements supplied directly to construction | Establish whether the constructor can implement the intended behavior under the same budget. Mark missing contract fields, adapters, and unsupported input signatures individually. Keep example-assisted construction in a separate column when the English request provides no examples. |
| A supplied correct program IR, on backend failures only | Check the interpreter and both emitters. A passing supplied implementation establishes expressibility. Credit for synthesis requires the constructor to discover the implementation. |

If a reference requirement cannot be expressed or passed to the constructor, the next implementation is the smallest shared typed contract and adapter that can carry it. Repeat the structured control first. If the contract reaches construction and still fails while supplied IR executes, investigate synthesis. If the structured route works and English fails, proceed to step 3. Where multiple failures exist, use only cases with a working structured route to attribute end-to-end gains to interpretation, and also report the full-suite result.

### 3. Compare interpretations on equal terms

Give every interpretation path the same typed target: operations or desired properties, typed arguments, references, scope, required structures, and explicit unknowns. Keep source evidence with each proposal. Run the symbolic path as a baseline. Freeze primitives, constructor, emitters and validators across comparisons. Measure separately whether the intended contract was ever proposed, whether it won, and whether both programs passed.

Use the development failures to choose **one** next intervention:

| Observed pattern | Experiment and comparison |
| --- | --- |
| Correct meaning absent despite available target operations | Evaluate a compositional semantic parser trained on English paired with typed meanings. As an offline reference, assess an existing toolkit such as Stanford SEMPRE for its ability to express the target and enumerate these meanings. Define the target operators once. Train or score how spans bind arguments and combine into trees. Compare on held-out trees and role reversals. Record data and grammar authoring costs. Do not port an external runtime to C++ based on development fit alone. |
| Correct meaning often proposed but the wrong one selected | Keep the candidate pools fixed. Compare symbolic ordering with a small discriminative ranker on identical pools. Log wrong acceptances and abstentions as well as correct selections. |
| No ACE reading blocks requests that another interpretation path can handle | Run the same coding front end on the original words, first with ACE evidence disabled, then available as an optional source. Compare paired gains, regressions and latency. Decide ACE's coding role from that result. |

For the learned-parser trial, pair development utterances with small typed trees. Derive additional training pairs from valid compositions and separately collected paraphrases. Give learned and symbolic approaches the same primitive definitions and access to the same development annotations. Track which combinations and paraphrase sources are reserved before generating any training data. The reusable part of a semantic parsing toolkit is its composition and search machinery; the experiment measures how much domain supervision unllm actually needs. [SEMPRE toolkit](https://www-nlp.stanford.edu/software/sempre/)

Use training and calibration cases to choose thresholds, including abstention. If multiple causes appear, prioritize the one responsible for the most development failures on backend-capable cases and document ties before testing. A correct label such as “loop” earns no contract credit if the loop binds the wrong collection or encloses the wrong action. A missing primitive is a separate capability decision. A corpus expansion needs a diagnosed data gap. Keep a Serenade vocabulary trial or a full model outside this comparison unless a later, separately specified question calls for it.

### 4. Make a decision from execution

Score each request once, and report Python and C++ failures separately. A positive end-to-end pass requires both outputs to pass independent execution cases and any explicitly requested structural check. Record incorrect accepted contracts, incorrect claimed completions, appropriate clarifications, correct stops, abstentions on supported tasks, latency, and search exhaustion. Inspect every changed decision; gains elsewhere cannot hide a new wrong acceptance.

For this feasibility study, advance a candidate to independent confirmation only if it produces additional both-language passes on at least two previously unseen, structurally different compositions, preserves existing verified passes, and introduces zero new wrong acceptances. These are proposed advancement criteria. Report inherited errors and absolute counts. Confirmation requires fresh prompts, unchanged code and training data, and the applicable existing regression suites. If the candidate misses the criterion, log the first failing stage and stop that line of work. Do not tune on those prompts and rescore them as fresh. This milestone makes a claim only about the evaluated constructs; broader application and project construction remain separate roadmap stages.

## Baseline and scope

The resident symbolic interpreter scored 20/32 on the development comparison:
12/24 supported requests interpreted correctly and 8/8 appropriate stops, with
zero incorrect accepted meanings. Its median interpretation time was 0.763 seconds
and p95 2.563 seconds. The cold CLI still rejected the mixed request that the
resident parser accepted. Those measurements are context-specific, not proof of
deterministic results across process histories.

The twelve missed supported cases expose these restrictions:

| Observed restriction | Cases | Planned treatment |
| --- | --- | --- |
| Literal creation-word gate | text paraphrase, fractional initial value, shopping description, discount description, pause request | Carry explicit code intent from the request context into grounding. Preserve negation and conflicting intent. |
| Missing grammatical/modifier handling | separate actions, task checkbox description, tax description using an amount | Ground predicates and modifiers through semantic roles and typed constraints. |
| Missing equivalent change/conversion constructions | counter goes down, temperature converter from Kelvin into Fahrenheit | Bind change direction/magnitude and unit source/target independently of verb versus noun realization. |
| Exclusions and richer descriptions | no-save text tool, multiple metrics with no buttons/multiline | Represent scoped forbidden effects and explicit modifier meaning. |

Removing the creation gate alone does not guarantee completion of those five
requests: later role or lexical failures must still be resolved and measured.
Useful new cases include initial values, multiple actions, derived text metrics,
record controls, clocks, percentages and conversion. Expanding GUI primitives,
arbitrary algorithm discovery, new languages and new application recipes are
outside this phase. Cardinality and representation limits remain explicit; a
request for multiple objects cannot be silently collapsed into one.

## Milestones and order

### 1. Establish repeatable parsing and useful failure traces

Preserve the experiment's raw outputs. Reuse its independent human annotations in
model-independent evaluation tooling, without importing the model extractor or
its JSON Schema dependency into the normal path.

Run the same requests with a fresh parser, repeated requests in one resident
parser, unrelated requests before them, and shuffled request orders. Capture the
original and normalized text, lexical span mappings, parser/grammar configuration,
candidate count, timeout/enumeration limits, canonical semantic candidates and
every distinct grounding rejection. Compare candidate sets and resulting contracts
to explain the cold/resident discrepancy before choosing its repair.

Give structurally equivalent readings stable identities without erasing scope,
negation, units or modifiers. Collect meaningful rejection reasons rather than
returning the first unrecognized predicate from the first reading. An unhandled
predicate such as loc_nonsp is evidence to investigate, not permission to ignore
the relation. Make genuine ambiguity, unsupported meaning and exhausted search
distinct outcomes.

Completion gate: the mixed request and reordered control produce consistent
canonical results across cold/resident/reordered trials, or explicitly report a
resource limit instead of inconsistent semantic claims. A fully grounded supported
reading must not depend on incidental request history within the configured test
budget. The trace identifies why every failed development request stopped.

### 2. Introduce an explicit typed requirements graph

Separate request interpretation from immediate widget/spec construction:

```mermaid
flowchart LR
    Q[Request plus explicit context] --> M[ACE/ERG MRS]
    M --> R[Typed requirements and source evidence]
    R --> B[Role binding and consistency checks]
    B --> S[Behaviour specification]
    S --> P[Existing planner and cvc5]
    P --> E[Existing Python/C++ emission and validation]
```

Represent program intent, state/entities, observable derived values, actions,
targets, initial values, operation direction, magnitudes, unit roles, required
effects and scoped exclusions. Requirements can initially contain unresolved
typed variables. Distinguish a derived character count from an editable numeric
counter, and an initial value from a transition operand. Preserve literal signs
and apply the operation exactly once.

Each requirement retains its source span, grammatical predicate/arguments, mapping
rule and status: explicit, justified default, excluded, or unresolved. Maintain
an offset map from linked grammar text back to original text. Defaults come from
declared capability contracts and are separately identified. Explicit requirements
and exclusions take precedence over optional defaults; contradictory explicit
requirements require clarification.

Keep this representation independent of program labels and emitted variable names.
Its lowering produces the existing BehaviourSpec. The mathematical solver checks
declared arithmetic; it cannot establish that English was interpreted correctly.

Completion gate: structured requirements for existing supported cases lower to
equivalent behavior. A fractional start value and update amount remain separate;
decrease-by-11 remains subtraction of positive 11; a character metric introduces
no extra counter control. Incomplete requirements cannot be lowered silently.

**Development result, 2026-11-18.** The C++ grounder builds requirements for compositional GUI behaviors before it creates state, actions or widgets. The selected specification carries the rules and source evidence under `interpretation.requirements`. Diagnostics can retain the graph for each reading, including blocked exclusions and unbound numeric roles. The specialized calculator and stream paths use their existing interpreters.

All 32 frozen requests retained their decisions and the 12 accepted programs retained their prior behavior. A generated C++ counter started at 8.5 and reached 4 after two decrements of 2.25. A generated Python counter reached 13 after two decrements of −2.25. The original “Give me … takes away” fixture and two additional phrasings still reached the 128-reading limit. Milestone 3 will test explicit code intent and grammatical role binding for these cases and ambiguous save targets. [Measurements](../experiments/interpretation_diagnostics/requirements-results.json)

### 3. Carry intent and bind grammatical roles through constraints

Explicit --code mode already establishes a programming request. Preserve that
intent through the semantic bridge so grounding does not demand a second literal
create/make/build word. In ordinary conversation, intent still needs grammatical
evidence; negated creation and unrelated factual statements remain distinct.

Use MRS arguments, compounds, conjunctions, relative clauses, prepositions and
scope to form candidate bindings. Domain lexical definitions supply concepts,
types and ordinary equivalent senses. Initial value, change direction/magnitude,
measurement result and unit direction receive explicit roles. Descriptions such as
goes down, takes away, starting at and converter from/into map to these roles.
Coordination distributes actions to their grammatical targets while retaining
separate operands and explicit shared scope.

First implement a small role-binding feasibility slice in the existing embedded
SWI-Prolog interface: a counter with distinct initial/update values and a save
action with two possible targets. Require a trace of role choices and rejections,
bounded candidate search and measured overhead before migrating the broader
grounder. Prolog owns dynamic binding and constraint decisions; Soufflé retains
static capability/type compatibility. There is no per-request background compiler
invocation. cvc5 retains arithmetic/synthesis ownership.

The [first embedded SWI trial](../experiments/interpretation_diagnostics/role_trial/README.md) recovered the two existing numeric controls and preserved ambiguous save targets on a parseable request. The coordinated counter still exhausted its readings. Inspect the five two-verb readings and test competing attachments before migrating production binding.

Lexical entries describe reusable meanings. They cannot recognize a whole test
sentence and return its prepared specification. Surface regex fallbacks, program
name routing and a larger catalog of whole applications are not the design.

Completion gate: the five creation-gate cases pass intent recognition; numerical
roles, separate actions, list controls, noun/verb conversion descriptions and
equivalent change expressions ground correctly in their full requests. Reordering
equivalent clauses preserves the canonical requirements. Replacing one operand
changes only its bound update. Missing targets and incompatible types remain
visible rather than being guessed.

### 4. Handle exclusions and targeted clarification

Represent no-save/no-buttons requirements as scoped constraints instead of
rejecting every request containing negation. An exclusion affects its target and
scope; unrelated metrics and transformations remain required. Required and
forbidden effects are checked together before lowering and component planning.

When multiple complete bindings differ only in an unresolved choice, return a
targeted question with the alternatives: save text or list; reset timer or counter;
which fixed percentage? Keep the original requirements so an answer can resolve
that slot without losing the rest. Unknown operations, incompatible dimensions
and hard capability limits remain explicit unsupported outcomes. Search exhaustion
cannot masquerade as a successful interpretation or a confidently unsupported
meaning.

Completion gate: no-save and no-buttons cases retain their positive requirements.
Save/reset ambiguity and missing fixed rate produce the correct clarification;
supported replies bind the pending role. Spell checking, encryption, remote sync
and incompatible units still stop appropriately. Every original refusal case keeps
its declared contract unless a capability expansion is separately versioned.

### 5. Validate interpretation against requests, then exercise generated programs

Keep two separate checks:

1. Request to specification: independently annotated requirements, including
   omissions, additions, values, units, targets, scope, defaults and refusal type.
2. Specification to program: build/run checks plus external Qt interaction and
   file assertions derived from the original request.

The experiment demonstrated why both are necessary: the wrong decrement
specification compiled and validated while displaying 28.5 instead of 6.5.
Do not use the candidate specification as the only acceptance oracle.

Use the existing 32 cases as development regression data. Before implementation,
freeze a further 30 validation descriptions: 20 supported paraphrases/combinations
and ten ambiguity, contradiction or unavailable-feature cases. Keep expected
outcomes separate from runtime interpretation and construction. Freeze the
implementation before the final validation run. Only call the evaluation blind if
authorship/access is actually independent; otherwise label it development
validation. Do not inspect the pre-existing sealed question sets.

Add paired checks for coordination order, creation wording in code mode,
fractional/negative initial values, changed operands, reversed unit direction,
pause/stop equivalence, derived count versus editable state, and scoped negation.
Pairs test relations between meanings, not a list of full-prompt substitutions.

Choose eight distinct request compositions before the final run and generate each
in Python and C++ through the actual CLI. Include the mixed request, initial-value
counter, two-action counter, list save/load, timer/text combination, conversion,
percentage and scoped text tool. All sixteen outputs must pass independently
specified keyboard/mouse/file/timer workflows. Preserve failed and unsupported
attempts in the denominator. Retain C++ normal/sanitizer runs and all original
regression suites. Run broad executable checks at the final milestone or when
changes affect executable behavior; use focused meaning tests during grounding work.

## Completion criteria

| Measure | Required result |
| --- | --- |
| Known 32-case development set | At least 30/32 correct, including at least 22/24 supported requests. Every case remains in the report. |
| Incorrect accepted interpretations | Zero on both development and fresh validation sets. A wrong target, omitted feature or invented control counts as wrong. |
| Original eight refusal/clarification cases | All handled appropriately; distinguish ambiguity, unavailable behavior and exhausted resources. |
| Fresh validation set | At least 18/20 supported descriptions correct; all ten negative/ambiguous cases stop or clarify correctly. No claim of blind evaluation without independence. |
| Parser consistency | Same canonical meaning/status across cold, resident, repeated and shuffled trials within declared limits; resource exhaustion explicit. |
| Generated programs | All sixteen preselected Python/C++ outputs pass independent original-request workflows. |
| Existing regressions | All twelve current CTest suites pass; factual, legacy, calculator and utility behavior preserved. |
| Interpretation latency | Target warm p50 at most 1 second and p95 at most 3 seconds on the declared workload, excluding construction. Report cold and search-limit cases separately. |

These are proposed acceptance targets, not predicted scores or completion-time
estimates. Keep the existing four-second explicit-code parser configuration and
separate factual deadlines as the baseline. Any necessary budget change is a
separately reported configuration, with its own coverage/latency results. Component
compilation remains a separate generation cost. Optimize measured parsing/binding
costs after correctness; versioned caching follows consistency work.

## Planned implementation boundaries

| Area | Proposed work |
| --- | --- |
| upgrade/semantics.py and structured request context | Intent provenance, traceable parser outcomes, source mappings and stable candidate handling. |
| New synthesis/grounding module | Typed requirements graph, MRS adapters, coverage accounting and lowering to BehaviourSpec. |
| reasoning/prolog/program_grounding.pl and native adapter | Bounded typed role binding, exclusion/consistency constraints and inspectable witnesses, following the feasibility slice. |
| data/program_semantics.json and lexical linker | Data-driven concepts/modifiers/senses and source-preserving linking. |
| synthesis/behaviour/english.py and frames.py | Integrate the graph/binder and retire duplicated eager intent/word checks. Existing primitive builders remain reusable. |
| Model-independent evaluation tooling and tests | Request-level annotations, metamorphic checks, cold/resident trials, targeted clarification and actual CLI/GUI acceptance. |

The local-model experiment is preserved as evidence. Its expected requirements
and independently confirmed successes can inform tests; model output is never
silently promoted into runtime language rules or authoritative knowledge.
