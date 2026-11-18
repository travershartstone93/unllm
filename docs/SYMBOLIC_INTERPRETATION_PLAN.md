# Plan: improve symbolic request interpretation

Proposed on 2026-11-12, following the [local-model experiment](LOCAL_MODEL_EXPERIMENT.md).
This document is a plan; runtime implementation has not started.

Implementation order update: the [C++ runtime migration](CPP_RUNTIME_MIGRATION_PLAN.md)
comes first. This roadmap is deferred until migration and pilot evaluation. Current
Python filenames below identify existing responsibilities to replace; future host
implementation uses native modules. PyDelphin remains a migration reference, not
a runtime dependency.

Scope update: begin with the [targeted symbolic distillation pilot](TARGETED_SYMBOLIC_DISTILLATION.md)
for numeric-change grounding. The wider milestones and 30/32 target below remain
a later roadmap, not prerequisites or scope for that first pilot.
The reviewed pilot starts with a discovery-only feasibility check, freezes its
engineering-only baseline, and reserves structural combinations for final
evaluation. Its contribution and stopping gates are defined in that pilot document.

The objective is to understand more ordinary descriptions of already supported
program behavior while preserving accurate operands, scope and targets. Work stays
in unllm-upgrade. ACE/ERG with native MRS handling, SWI-Prolog, Soufflé and the existing typed
construction/cvc5 backends remain the tools. The local-model experiment stays
isolated and optional. The original checkout and carried user edits are preserved.

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
