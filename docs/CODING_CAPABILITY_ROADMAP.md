# Roadmap: broader symbolic coding capability

Proposed 2026-11-13. Progress updated 2026-11-18. This connects the C++ migration, interpretation
work and symbolic distillation to the user's objective: construct useful unfamiliar
programs without implementing each requested application's solution ourselves.

The [numeric pilot results](../archive/symbolic_distillation/numeric_change/README.md) record the Stage 13a feasibility stop. The [Stage 14 experiment](../experiments/algorithm_ir/README.md) implements and exercises the bounded algorithm representation from supplied specifications. The [first Stage 15 development evaluation](../experiments/algorithm_ir/SYNTHESIS.md) constructed candidates that passed reserved cases for ten of twelve contracts in both languages.

## What remains after the C++ port

The [C++ migration](CPP_RUNTIME_MIGRATION_PLAN.md) changes implementation language
and removes host Python dependencies. It preserves the current capability boundary.
The [numeric distillation pilot](TARGETED_SYMBOLIC_DISTILLATION.md) tests whether
verified symbolic knowledge can improve interpretation of existing capabilities.
Neither, by itself, enables synthesis of unfamiliar algorithms.

This roadmap adds that missing capability track. It remains entirely C++ on the
host, with the selected native engines and declarative rules. Python and C++ remain
output languages; no generative model runs in the normal interpretation, planning,
construction or repair path.

The intended result is a progressively broader, guided coding tool. Success on
these stages is not a promise of unrestricted natural-language programming or
automatic work on arbitrary repositories.

## Sequence and dependencies

| Stage | Purpose | Evidence of progress |
| --- | --- | --- |
| 12: native migration | Correct the implementation architecture. | Current behavior works through C++ host code, including no-Python deployment checks. |
| 13a: targeted distillation pilot | Establish a bounded method for acquiring reusable semantic rules. | Held-out structural combinations improve over engineering-only fixes, with no observed wrong acceptances. |
| 13b: interpretation reliability | Preserve roles, intent, scope and exclusions; ask useful clarification questions. | Requests map to independently checked requirements, including incomplete/ambiguous cases. |
| 14: general algorithm representation | Express computations beyond the existing fixed behavior/effect catalog. | One typed representation describes several distinct small algorithms and executes equivalently in both output languages. |
| 15: bounded algorithm synthesis and repair | Construct missing program structure from requirements and evidence. | Unseen tasks succeed with frozen primitives/search code, without task-specific patches. |
| 16: acquire reusable coding knowledge | Distill verified decomposition rules or helper abstractions offline. | Each retained addition transfers to new tasks and improves a frozen baseline. |
| 17: practical interaction and performance | Combine understanding, clarification, synthesis, execution and revision. | Independent end-to-end workflows, measured latency and honest failure handling. |

Stage 12 precedes implementation of the capability work. Evaluate 13a before
expanding distillation, but a failed pilot does not prohibit ordinary engineering
or structured-specification synthesis. Stages 13b and 14/15 can advance separately
after native interfaces stabilize: do not wait for perfect English understanding
to test whether the backend can construct algorithms. Stage 16 requires evidence
from both the acquisition pilot and the algorithm engine; it is not presumed to work.

The first algorithm release is a small slice of stage 15. Later stages are planning
boundaries, not authorization to implement every feature simultaneously.

## 13: Better request interpretation

Retain the existing [interpretation roadmap](SYMBOLIC_INTERPRETATION_PLAN.md):
repeatable parse diagnostics, typed requirements with source evidence, grammatical
role binding, scoped exclusions and targeted clarification. Implement in migrated
C++ modules, without surface-regex fallbacks or whole-request lookup tables.

The coding investigation compared English input, independently supplied requirements, and supplied program IR. The native example-contract bridge passed. A learned parser improved bounded stream construction and failed the broader coding gate. The next interface question is whether supplied requirements for function signatures, behavior, and explicitly requested nesting can survive construction. Prefer established industry solutions for the diagnosed failure class. Document any remaining gap before building a custom mechanism and test it on unseen combinations. [Evaluation and decision rules](SYMBOLIC_INTERPRETATION_PLAN.md#english-to-program-investigation) · [Measured scope](../archive/interpretation_diagnostics/n16/broad-results.md)

The numeric pilot remains three semantic families and its existing example/search
caps. Its success establishes transfer of those rules, not generic programming
knowledge. Afterward choose the next interpretation slice from measured failures;
do not collect synonyms indefinitely while leaving algorithm construction unchanged.

An algorithm request must eventually specify typed inputs/outputs, observable
behavior, constraints, examples, errors and any unresolved choices. Separate what
the user explicitly requested from defaults. A clarification fills a particular
missing slot and retains the other requirements. Examples alone may admit several
programs; do not pretend they uniquely define the user's intention.

## 14: A representation that can express algorithms

Introduce a separate typed algorithm representation rather than extending the
GUI effect catalog with entries named after each new task. Keep the migrated
stream/calculator/behavior paths working; they are compatibility clients, not the
definition of all future code.

The first slice supports pure functions over bounded integers, Booleans and
integer sequences, with:

- Named parameters, local variables and explicit return values.
- Typed arithmetic, comparisons, branches and checked indexing.
- Sequence construction, append, length and element access.
- Iteration over input elements or a bounded index range, carrying explicit state.
- Small helper functions with an acyclic call graph.

Declare overflow, empty-input behavior, mutation/aliasing rules and evaluation
order. Use checked signed 64-bit integers consistently in Python and C++ output;
Python's larger integers must not silently create different semantics. Floating
point, dictionaries, arbitrary records, recursion and unrestricted while loops are
later extensions, each requiring its own semantics and search gate.

Initial experimental limits: inputs of at most 64 elements, at most four helper
functions, eight locals per function, two nested bounded loops and 80 algorithm
nodes per request. Freeze those limits before evaluation. They bound the pilot,
not the ultimate language. Report out-of-domain requests explicitly.

Build an interpreter and both emitters for this representation. First use manually
specified algorithms to check representation and execution fidelity. These checks
must not be counted as successful algorithm synthesis. Then test structural
renaming, different coefficients and independent inputs in both languages.

The acceptance question is whether general control/data operations express several
different computations without editing an application-specific constructor. Source
line count remains secondary: a short newly constructed algorithm is stronger
evidence than a long GUI containing familiar handlers.

## 15: Construct algorithms, not only scalar expressions

Accept typed specifications directly as well as requirements obtained from English.
This permits separate measurements of language understanding and construction.
An explicit optional program sketch can constrain structure; label assisted and
unassisted results separately rather than concealing supplied algorithmic guidance.

Use typed, budgeted search over the new representation. Start from general
constructs such as iteration, accumulation and branching. Prune candidates using
types, bounds, effects and intermediate obligations; canonicalize equivalent
choices where justified. Do not enumerate arbitrary Python/C++ source text.

Keep engine roles narrow: native C++ owns the representation, candidate management
and execution; Prolog can select applicable decomposition rules; cvc5 can solve
supported expression/constraint subproblems. Existing Soufflé compatibility checks
remain useful where applicable. None is assumed to solve arbitrary loops merely
because the representation contains them.

Make the candidate loop explicit:

1. Establish a complete enough specification or ask for a missing requirement.
2. Select a bounded structure and identify smaller obligations/holes.
3. Construct candidate expressions and control/data flow.
4. Check independent examples, properties and applicable solver obligations.
5. Use a development counterexample to reject/refine the candidate.
6. Emit and exercise both languages before reporting validated completion.

Search exhaustion, inconsistent requirements and demonstrated unsupported behavior
are different outcomes. Failed search is not proof that no implementation exists.
Tests provide finite evidence; proofs retain their stated domains and assumptions.
Frozen acceptance cases are used for scoring only, never fed into repair.

Initial search budget: 120 seconds total for candidate search per request, including
its refinement iterations. Keep a separate reported compilation/execution budget.
Freeze solver sub-budgets and candidate limits after the feasibility slice, before
the final evaluation. If search is impractical, narrow the development experiment
or require an explicit sketch in a separately scored mode; do not add the failed
task's complete solution as a new primitive and count that as synthesis.


### Recursive construction, structural editing, and autonomous repair

The synthesis engine must support recursive construction and revision of programs.
Generating a single candidate, validating it, and stopping is insufficient for
the intended coding capability.

The objective is to construct programs incrementally, identify failures, revise
affected structures, and repeat until the specification is satisfied or an
explicit resource or capability boundary is reached.

This mechanism must operate entirely through native C++ host code, typed program
representations, symbolic reasoning, constraint solving, and independent
validation. No generative language model is permitted in the construction,
editing, diagnosis, or repair path.

#### 1. Persistent program representation

Introduce a versioned program session that retains sufficient information to
continue construction or editing across multiple requests.

The session must contain:

- A stable program identifier and revision number.
- The original request and its interpreted requirements.
- The current typed specification and algorithm representation.
- Stable identifiers for individual requirements and program components.
- Active, modified, withdrawn, and superseded requirements.
- Function signatures, contracts, dependencies, and unresolved obligations.
- Selected primitives, decomposition rules, and solver witnesses.
- Validation results, counterexamples, and regression tests.
- Previous construction attempts and their failure classifications.
- The last independently validated program revision.
- Generated-source hashes and target-language information.

Session state must be serializable and recoverable where supported by the
product interface.

The typed program representation remains authoritative. Generated Python and
C++ source files are products of that representation, not the primary objects
being modified.

Existing schema-2 behavior specifications and supported application paths must
remain compatible.

#### 2. Recursive program construction

Permit a programming requirement to be decomposed into smaller typed
obligations.

Each obligation may be:

1. Satisfied directly by a verified primitive or helper.
2. Reduced to a supported expression or constraint-solving problem.
3. Decomposed into additional subgoals.
4. Filled through bounded enumeration of typed program structures.
5. Rejected as ambiguous, inconsistent, unsupported, or resource-limited.

An obligation may introduce typed holes representing unresolved expressions,
conditions, operations, control flow, or helper implementations.

The construction engine must maintain the relationship between each hole and
the requirements it is intended to satisfy.

For each candidate:

1. Select an unresolved obligation.
2. Identify applicable primitives, decomposition rules, or candidate structures.
3. Construct or refine the corresponding typed representation.
4. Check types, contracts, effects, and supported formal constraints.
5. Execute applicable independent tests.
6. Retain validated components and reject failed candidates.
7. Propagate new constraints or counterexamples to unresolved obligations.
8. Repeat until the complete program passes validation or the budget expires.

The engine must support revisiting earlier construction decisions when later
constraints demonstrate that a previously selected decomposition is unsuitable.

A locally successful subprogram does not establish that the complete program is
correct. Final validation must cover the composed program and its observable
behavior.

Recursive decomposition and search must have explicit depth, node, time, and
candidate limits. Unbounded self-recursion or unrestricted search is not
required by this milestone.

#### 3. Transactional structural editing

Support modifications to an existing program without treating each instruction
as an entirely new programming task.

Initial editing operations should include:

- Adding, removing, or changing a function or helper.
- Modifying a function parameter, return type, or contract.
- Replacing an expression, condition, or operation.
- Adding, removing, or restructuring a branch.
- Adding or modifying a bounded loop.
- Changing a collection transformation.
- Updating a dependency or function call.
- Adding or withdrawing a requirement.
- Renaming a program component while preserving valid references.

All edits must operate on the typed specification or algorithm representation.

An edit must:

1. Identify the requirement or structure being changed.
2. Determine its dependent components.
3. Preserve requirements that have not been explicitly superseded.
4. Apply the proposed modification to a new candidate revision.
5. Revalidate affected components and applicable prior requirements.
6. Commit the new revision only after its acceptance conditions pass.
7. Otherwise restore the previous valid revision.

Multiple dependent changes must be atomic: either the complete modification is
accepted or the previous valid state remains unchanged.

Rebuilding the entire program after a structural edit is permitted initially.
Incremental dependency-based reconstruction is a later performance optimization.

Do not patch emitted source using request-specific regular expressions or
textual substitutions. A separate source-import capability would be required
before arbitrary user-modified source could become authoritative.

#### 4. Autonomous diagnosis and repair

When a candidate fails validation, the system must attempt to identify the
smallest relevant program structure or construction decision responsible for
the failure.

Represent failures as structured diagnostic records rather than relying only
on compiler output or free-form error messages.

Initial diagnostic categories should include:

- Invalid or incomplete requirements.
- Type or contract violations.
- Unsatisfied solver constraints.
- Incorrect expression or condition.
- Incorrect control-flow structure.
- Missing or incompatible dependencies.
- Incorrect collection operations.
- Loop-bound, indexing, or termination violations.
- Incorrect observable output or state transition.
- Backend emission or compilation failure.
- Runtime error or failed independent test.
- Resource exhaustion or unavailable infrastructure.

Different failure classes must trigger different repair strategies.

For example:

- An incorrect expression may be re-synthesized using its counterexample.
- An incorrect branch may require revising its predicate or control structure.
- A missing operation may require revisiting the decomposition or dataflow graph.
- A dependency failure may require selecting another compatible component.
- A backend emission defect must be attributed to the corresponding lowering
  operation rather than silently changing a valid algorithm.
- An ambiguous requirement must trigger clarification rather than speculative
  modification.

Infrastructure failures must not be misclassified as program defects.

#### 5. Counterexample-guided recursive repair

Extend the existing counterexample-guided synthesis process to support
repeated structural revisions.

The repair cycle is:

    specification
        -> construct candidate
        -> validate
        -> classify failure
        -> identify affected structure
        -> propose structural modification
        -> recheck constraints
        -> revalidate
        -> accept or retry

A counterexample must be associated with the requirement, component, or
contract it demonstrates to be unsatisfied.

The repair controller may modify expressions, control flow, component
selection, or decomposition choices within the declared search space.

It must not weaken or silently remove requirements merely to obtain a passing
candidate.

Prefer repairs that:

- Preserve previously verified components.
- Minimize changes to the typed representation.
- Preserve unaffected requirements.
- Introduce fewer new dependencies.
- Avoid repeating previously rejected candidates.
- Reduce unresolved obligations or improve independently checked correctness.

These preferences guide search order; they do not override correctness.

Each repair attempt must be bounded by the existing synthesis search budget.
Maintain candidate hashes and visited states to detect repeated attempts and
cycles.

A failed repair must not corrupt the last valid program revision.

#### 6. Recursive editing across user requests

The same mechanism must support sequential user instructions.

For example:

Initial request:

    Construct a function that filters positive integers.

Revision 1:

    Remove duplicate values from the result.

Revision 2:

    Preserve the original order.

Revision 3:

    Add an optional parameter that reverses the final result.

The engine must update the existing program representation while retaining
requirements that remain active.

After each revision:

- The revised specification must be inspectable.
- Changed and preserved requirements must be identifiable.
- Previous applicable regression tests must be rerun.
- Both output languages must implement equivalent behavior.
- Rejected revisions must leave the preceding valid program available.

A revision must not be treated as successful merely because the latest requested
feature works; the resulting program must continue satisfying all applicable
previous requirements.

Ambiguous references to earlier components must be resolved through structured
session context or clarification.

Arbitrary editing of external repositories remains outside this milestone.

#### 7. Search history and future adaptive learning

Record structured information from construction and repair attempts:

- Requirement and contract identifiers.
- Candidate representation hashes.
- Selected primitives and decomposition rules.
- Search depth and candidate cost.
- Solver outcomes.
- Validation failures and counterexamples.
- Successful repairs and rejected alternatives.
- Time and resource consumption.
- Final validation status.

This information must be available for profiling and reproducible evaluation.

It may later support probabilistic program grammars, adaptive candidate ordering,
or lightweight learning from previous synthesis attempts.

No training corpus, pretrained model, or learned ranking policy is required to
complete this milestone.

Any future statistical search guidance must remain optional and must never
override typed constraints, formal obligations, or independent validation.

Reusable knowledge acquisition and learning experiments remain governed by
Stage 16.

#### 8. Acceptance and evaluation

Add a separate recursive-construction and editing evaluation alongside the
existing algorithm-synthesis benchmark.

The evaluation must distinguish:

1. Initial construction success.
2. Successful autonomous repair following a detected failure.
3. Successful user-directed structural revision.
4. Preservation of previously satisfied requirements.
5. Correct rejection of unsupported or inconsistent changes.
6. Repair attempts exhausted without an incorrect success claim.

Prepare independent seeded-defect cases covering expressions, branching,
dependencies, indexing, collection operations, and backend failures.

Include negative cases where no authorized repair can satisfy the original
specification.

At minimum, demonstrate:

- An unfamiliar algorithm constructed through multiple verified subgoals.
- A failed candidate repaired automatically using a counterexample.
- A structural repair that changes control flow rather than only a scalar value.
- A sequence of at least three modifications to the same program.
- An invalid revision rejected without changing the last valid program.
- Preservation of active requirements through all accepted revisions.
- Equivalent validated results from Python and C++ output backends.
- Bounded termination when the engine cannot find a valid program.

Freeze the repair evaluation cases and resource budgets before tuning.
Do not introduce task-specific complete solutions, special-case controllers,
or hidden reference implementations into the synthesis engine.

Report:

- Initial construction success rate.
- Autonomous repair success rate.
- Average and maximum repair iterations.
- Candidate structures explored.
- Requirements preserved or violated.
- Structural changes made per successful repair.
- Total synthesis and validation time.
- False validated completions.
- Unsupported, ambiguous, and exhausted outcomes.

The first release does not require perfect repair success. It requires
demonstrable improvement over single-attempt construction, reproducible
failure handling, and no incorrectly reported validated completions.

#### Completion criterion

This capability is complete when the same frozen native C++ construction
engine can independently construct, diagnose, modify, and revalidate unfamiliar
supported algorithms across multiple iterations without task-specific solution
templates or a generative language model.

Successful recursive iteration must demonstrate changes to program structure,
not merely repeated generation of equivalent candidates.

The result must remain inspectable, bounded, reproducible, and compatible with
both Python and C++ output backends.

### First algorithm-synthesis evaluation

Use at least three classes of computations: sequence transformations, stateful
scans, and conditional selection/aggregation. Prefix calculations, first-match
search and adjacent-value processing are illustrative development work, not secret
acceptance tasks. Define expected empty-input, boundary and overflow behavior.

Before search tuning, freeze twelve supported task contracts and six incomplete,
contradictory or out-of-scope cases. Partition by algorithm structure, not merely
numbers, names or paraphrases. Keep whole acceptance tasks, expected solutions and
test cases outside acquisition/search-tuning inputs. Record who authored and saw
them; call the evaluation development validation unless independence is real.

Proposed first-release gates:

- At least eight of twelve supported tasks independently pass in both languages,
  with successes across all three classes. All attempts remain in the denominator.
- No incorrectly reported validated completion; all six stop/clarification cases
  handled according to their annotated contracts.
- The same frozen primitives, decomposition rules and search implementation serve
  every acceptance task. No per-task branch, primitive or answer-table addition.
- Chosen implementations expose their algorithm representation, search/validation
  trace, reusable helper calls and stated limits for inspection.
- Existing native factual and program-generation regressions still pass.

This proves a bounded increase in construction capability, not unrestricted coding.
A failure is useful evidence about representation, specification or search; it
does not justify increasing the release scope to conceal a weak result.

## 16: Symbolic distillation of coding knowledge

Only after the earlier gates, test a separate offline knowledge-acquisition slice.
Potential outputs are typed decomposition rules, verified helper functions or
search-order hints. They are versioned artifacts, not a runtime teacher connection.

A proposed helper has an interface, preconditions, postconditions, implementation
in the algorithm representation, independent tests, provenance and a cost estimate.
A decomposition rule explains when it applies and how its subgoals satisfy the
parent requirement. Search hints may affect ordering but cannot bypass validation.

Require transfer to at least three structurally distinct fresh tasks per proposed
package. Compare a frozen engine without the package to the same engine with it,
using identical budgets. Report correctness, completion, search cost and manual
semantic edits. A helper containing the exact answer to one task is not sufficient
evidence of reusable acquired knowledge. Keep rejected proposals and rollback each
package independently. Freeze a small package-specific acquisition budget before
running a teacher; do not scale data collection merely because the numeric pilot passed.

Learning a library primitive is legitimate when its contract supports reuse across
tasks. The objective is to stop implementing each requested solution, not to remove
all predefined operations or engineering knowledge from the system.

## 17: End-to-end usefulness and measured speed

The first English-to-algorithm slice accepts bounded integer interval goals through the normal coding CLI and the structured specification interface. Six of nine new in-family requests produced independently checked functions in both languages. All three misses lacked a correct candidate. Broader English construction, clarification and integrated revision gates remain open. [N13 results](../experiments/algorithm_ir/goal_selection/README.md)


Join the paths only after structured-specification synthesis works. Exercise fresh
requests, a required clarification, execution and a changed requirement. Confirm
that revisions preserve unaffected requirements and regenerate code from structured
meaning. Reuse existing file/GUI primitives to expose independently synthesized
pure functions where useful; do not make new application categories the milestone.

Profile parsing, binding, candidate search, solver work, emission, compilation and
validation separately. Optimize the dominant measured costs: examples include
versioned caches, reusable verified helpers, narrower typed search and incremental
rechecking. Record cache keys and invalidate on rule/specification/toolchain changes.
Do not promise that a C++ port, an added solver or additional rules automatically
make answers faster. Performance changes must preserve the validation gates.

Arbitrary repository editing, new frameworks/APIs, networking, unbounded recursion
and general autonomous application development remain outside the first release.
They need later capability contracts and evaluations; this roadmap does not count
them as solved by adding loops to an intermediate language.

## Reporting and next decision

Report three separate measures: interpretation fidelity, construction success from
correct specifications, and end-to-end completion. Also report wrong acceptances,
clarifications, resource failures, task-specific engineering additions and latency.
This makes it possible to tell whether more language rules or more synthesis work
would address the current bottleneck.

Stage 13b parser repeatability passed its [development comparison](../archive/interpretation_diagnostics/README.md). The native GUI grounder now lowers typed requirements and passes the [milestone 2 development checks](SYMBOLIC_INTERPRETATION_PLAN.md#2-introduce-an-explicit-typed-requirements-graph). The bounded role-binding trial reached its [experimental stop](../archive/interpretation_diagnostics/role_trial/README.md). The Stage 14 experiment covers nine supplied programs and 31 oracle cases. The first Stage 15 evaluation reached 10/12 development contracts in both languages, 6/6 correct stops and 5/5 seeded repairs. Reverse and adjacent difference remain open. Stages 16–18 and the independent release evaluation remain ahead. The storage inventory and USB migration are recorded in the [migration report](../experiments/corpus_storage/s0/MIGRATION.md).
