# Roadmap: broader symbolic coding capability

Proposed 2026-11-13. Planning only. This connects the C++ migration, interpretation
work and symbolic distillation to the user's objective: construct useful unfamiliar
programs without implementing each requested application's solution ourselves.

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

The immediate next implementation remains the C++ migration. Preserve the numeric
pilot, add the algorithm representation/search milestones to the main roadmap,
and use their bounded results to choose subsequent work. No runtime code, teacher
run or general-purpose capability is introduced by this document.
