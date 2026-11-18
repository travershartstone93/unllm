# Targeted symbolic distillation: first pilot

Design proposed and reviewed on 2026-11-12. Planning only; no teacher run or runtime
implementation has started.

Implementation order update: complete the [C++ runtime migration](CPP_RUNTIME_MIGRATION_PLAN.md)
first, then freeze a new native baseline for this pilot. The acquisition/evaluation
design below remains in force. References to current Python files document the
existing system; the pilot's host adapters, selector orchestration and verifier
must use the migrated C++ implementation. PyDelphin is reference tooling only,
not part of the eventual runtime. This pilot is not a prerequisite for the port.

Here symbolic distillation means extracting a small, reusable set of explicit
semantic rules from an offline teacher's examples, then independently checking
those rules before using them in unllm. The teacher supplies hypotheses and
evidence. Verified symbolic rules supply runtime behavior.

## Review decision

Keep this narrow pilot. Its separation of English fidelity from executable
correctness, plumbing-only control and bounded rule selection are sound. This
revision adds a feasibility gate, structural evaluation splits, operational label
review and explicit integration outcomes. It also distinguishes exact numeric
interpretation from floating-point execution and repairs the contribution gate.

Success would establish useful reusable numeric rules and a reproducible acquisition
process. It would not establish general language understanding or that a teacher
is better than ordinary rule engineering. No additional task family, learning
framework or arithmetic backend is included.

## One target

Improve interpretation of **one numeric counter, one addition/subtraction action,
and an optional initial value**, in explicit code mode. The backend already
supports this behavior. The pilot teaches how ordinary language binds its roles.

Representative development requests:

- Give me a counter starting at 8.5 whose button takes away 2.25.
- Make a counter that goes down by 0.125 each time I click its button.
- Build an app that decreases a counter by 11. This is an existing control case.

The three candidate semantic families are initial-value binding, directional change
(go up/down), and removal expressed as subtraction (take away). Existing
increase/decrease forms provide controls. Initial values may be negative; update
magnitudes in this first slice are nonnegative finite decimals. Negative or
missing magnitudes require clarification or an explicit unsupported result, not
an absolute-value guess or implicit update amount. An omitted initial value has
the declared default zero. Exactly one requested action is produced; the pilot
must not inherit the existing counter's three default buttons. Examples already
shown in plans or prompts count as development context, not unseen evidence.

Multiple counters/actions, clocks, units, percentages, lists, file effects and
general negation handling belong to later slices. Existing paths continue to
handle requests outside the slice. A numeric rule cannot consume just one part
of a larger request and silently drop its other requirements.

The pilot does not require the broad requirements-graph refactor or all twelve
known grammar failures to be fixed first. Its completion criteria are local to
this target, with the existing suite providing regression protection.
If it depends on general parser repair or the wider refactor, report that dependency
and stop instead of expanding the pilot.

## What the student must produce

Use a small typed numeric frame, not a whole app template:

| Slot | Meaning |
| --- | --- |
| intent | Explicit program-request context, retained from --code. |
| target | The unique numeric state referenced by the change. |
| initial | Exact decimal/rational value, with explicit or declared-default provenance. |
| operation | Addition or subtraction. |
| magnitude | The exact number grammatically attached to the operation. |
| evidence | Original text spans, actual MRS nodes/arguments, candidate identity and mapping rule IDs. |
| coverage | Requirement-bearing content explained by rules or declared structural handling, and any remainder. |
| unresolved | Missing, conflicting or ambiguous roles; these prevent successful lowering. |

For the first example, initial is 8.5, operation is subtract and magnitude is
2.25. The expected next value is 6.25. Decrease by 11 binds subtraction and
positive 11; the direction is applied once.

Lower this frame through the existing typed state/action builders and cvc5-backed
constructor. The model supplies no controller, executable code or final equation.
Math proofs establish the declared update; independent annotations check whether
the declaration faithfully represents the request.

Exact source quantities are retained for role checking; generated programs retain
the existing binary64 contract, +/-1e15 bounds and twelve-decimal-place numeric
widgets ([specification](../synthesis/behaviour/spec.py),
[solver](../synthesis/behaviour/solver.py),
[widgets](../synthesis/behaviour/python_runtime.py)). Do not claim exact decimal
execution. For this pilot, bound literals to six fractional digits and absolute
values at most 1,000,000, with independent workflows of at most twenty clicks
remaining within that range. Compare roles exactly and executable results against
independently computed decimal outcomes with absolute tolerance 1e-8 and zero
relative tolerance. Include 0.1 as well as exactly binary-representable fractions.
The pilot adds no support outside this numeric envelope; existing paths retain
their declared contracts.

## The offline process

```mermaid
flowchart LR
    F[Discovery-only feasibility and plumbing] --> T[Configurable offline teacher]
    T --> H[Proposed labels and symbolic hypotheses]
    H --> V[Independent label review and counterexamples]
    V --> R[Bounded Prolog rule selection]
    R --> C[Contract checks and frozen evaluation]
    C --> P[Versioned rule package]
```

### 1. Check feasibility and freeze the engineering baseline

Save baseline A: source, parser/grammar versions, budgets and current outcomes.
Use six seeds, two per family, drawn from the eventual discovery allowance. Keep
failed seeds in the record. Inspect actual MRS alternatives cold, resident and
after unrelated requests. Before teacher calibration or selector development,
establish that:

- At least one construction per family has sufficient parser evidence, expressible
  in the proposed finite rule vocabulary. Missing readings remain parser failures.
- A manually supplied numeric frame lowers to the specified initial state and
  exactly one action in both languages using the existing constructor. These are
  adapter checks, not evidence of English interpretation.
- An experimental adapter with no learned clauses preserves existing outcomes.

Define and freeze B, the engineering-only baseline, before clause proposals:

- Explicit --code intent, complete original-to-normalized source mapping, and the
  scope/variable properties needed by this slice. Preserve raw MRS offline.
- Candidate identities and observed timeout, fragment and enumeration status.
  Unknown search completeness must not be reported as exhaustive parsing.
- Numeric-frame lowering, fixed content-accounting guards and bounded Prolog
  evaluation. Helpers cannot hide new numeric construction meanings.
- Any request-envelope handling, such as Give me versus Build, declared separately.
  Removing the literal creation-word gate alone does not handle unfamiliar verbs.

These are real integration needs: the programming branch returns before some limit
diagnostics, the linker maps replaced terms rather than all shifted offsets, and
the counter helper hardcodes zero ([parser](../upgrade/semantics.py),
[linker](../synthesis/behaviour/lexicon.py), [frames](../synthesis/behaviour/frames.py)).
PyDelphin exposes arguments, scope constraints and alignment; preserve the evidence
required by the slice ([MRS API](https://pydelphin.readthedocs.io/en/latest/api/delphin.mrs.html)).
Do not build a generic requirements graph for this adapter.

If new request envelopes require substantial work, freeze existing build/make
forms and record the deferred examples before freezing acceptance. Do not narrow
scope after seeing acceptance results. Keep parser budgets and normalization
identical in B and C. A rule cannot invent a reading missing from parser output.

### 2. Freeze data boundaries, calibrate and acquire discovery examples

Before teacher labels or clauses, freeze the scope, rule vocabulary, split policy,
gates, calibration/acceptance sets and initial discovery labels. Later discovery
questions can adapt to observed disagreements; write their gold labels before
requesting their teacher predictions. Keep hashes and an authorship/access record.

| Data | Maximum | Use |
| --- | ---: | --- |
| Calibration | 12 | Fixed gold labels for checking the selected teacher. |
| Discovery | 36: 24 positive, 12 contrasts | Seeds, proposals and up to two selection iterations. |
| Acceptance | 24: 16 positive, 8 negative/ambiguous | Final evaluation after code and rules are frozen. |
| Existing comparison | 32 | Development regression protection, not fresh evidence. |

Gold labels specify complete expected roles, allowed defaults, exact stop reasons
and independent numeric outcomes. Write them before viewing teacher predictions;
record who authored/reviewed them and what they could see. A second invocation or
another teacher vote does not confer independence. If the same assistant authors
and reviews everything, call it development validation. Version discovery label
corrections. An ambiguous acceptance annotation invalidates the item and prevents
claiming the full gate; do not quietly replace it after seeing results.

Choose the teacher explicitly per run; no particular local model is assumed to be
good enough. It may be a capable model session or coding assistant. Its identity,
configuration, prompt version, outputs, token usage and review effort are recorded.
The earlier small-model results are evidence that teacher accuracy must be checked.

Use twelve public, manually annotated calibration examples covering direction,
initial versus update value, fractional values and ambiguity. The teacher must
preserve all required roles and correct stopping decisions on all twelve before
its proposed labels are used. A failed calibration stops that configuration's
run. Another configuration is a separately reported run. Calibration does not
certify later labels or clauses.

Acquire at most 36 discovery examples: 24 positives and twelve contrasts or
out-of-scope examples. Start with up to eighteen, including the six seeds; use the
remaining allowance only for observed disagreements or coverage gaps. Count
rejected/parser-incompatible proposals against the cap, not just retained examples.
A teacher may suggest unlabeled question text first; review it and write gold
before asking for its role prediction. Ask for structured bindings to actual
ACE/PyDelphin MRS output. Examples include the same wording with different
numbers, initial values separated from operands, missing amounts, multiple
possible targets, and non-arithmetic uses of down/away. Teacher explanations are
short references to evidence, not a substitute for checking the label.

Review each retained label against the original request and an independently
specified outcome. For a small numeric slice, this is a manageable annotation
task. Incorrect, uncertain or parser-incompatible examples stay in a rejection log;
they do not become training truth. A teacher cannot approve its own examples by
voting on them again.

The second iteration targets minimal pairs that distinguish surviving hypotheses:
initial versus operand attachment, arithmetic versus spatial down/away, missing
amounts, competing targets and an extra requested effect. Do not spend it merely
collecting more easy paraphrases. Acceptance stays unavailable to acquisition.

### 3. Distill a few general clauses

The teacher proposes clauses as data in a restricted typed format using a fixed
vocabulary of MRS predicates, arguments, modifiers, typed variables, quantity bindings and scope
relations. Trusted code translates it to Prolog. All output variables must be
bound; arbitrary calls, recursion and runtime I/O are excluded.
Predicate constants must exist in the observed grammar output. Full sentences,
example IDs, app names and particular numerical answers cannot serve as rule keys.
Numbers remain variables. Helpers encoding new linguistic meanings count as
semantic rules, not free plumbing.

The first implementation selects among proposed clauses and fixed typed rule
templates; it does not build a general-purpose inductive-programming platform.
Limit proposals to twelve clauses per family, six body conditions per clause,
and at most six selected clauses across the three families.

Use SWI-Prolog to evaluate candidate sets through the same evaluator and guards
used at runtime. Require complete expected frames on retained supported discovery
cases and no incorrect complete frames on any retained case. Partial role bindings
are assessed in combination: an initial-value clause matching an out-of-scope
request is not itself an erroneous complete interpretation.

Finite selection prefers fewer clauses, then fewer body conditions, with
deterministic tie-breaking. SWI's
[CLP(FD) enumeration and optimization](https://www.swi-prolog.org/pldoc/man?predicate=labeling/2)
can select that set; it does not invent the hypothesis language or establish
English truth. No feasible set or a search timeout is a reported failure.
cvc5 retains arithmetic ownership; existing Soufflé checks retain their role.

The current native bridge exposes reasoning and planning, not generic MRS binding
([bridge](../src/logic_bridge.cpp), [adapter](../upgrade/native.py)). Budget one
bounded numeric-binding operation, shared offline and at runtime, with fact,
solution and inference limits frozen in B. Do not start a compiler or a new Prolog
process for every normal request.

A rule must explain its successful bindings and its rejected alternatives. If two
selected rules imply incompatible changes or targets, return ambiguity. Do not
prefer whichever rule ran first. Every meaningful request component must be
accounted for before lowering a complete numeric request.

Define integration decisions explicitly; a successful partial rule is not an
accepted application:

| Module outcome | Required behavior |
| --- | --- |
| complete | One complete numeric contract, with all required content accounted for. |
| no_match | No supported complete match for this slice; existing handling may still resolve the entire request. |
| ambiguous / incomplete | Conflicting or missing roles. Do not turn this into a guessed/default counter. |
| unsupported_content | Numeric lowering would discard a requirement. No partial acceptance. |
| resource_limit | Parser/binder search exhausted its declared limit; no complete interpretation claim. |

Out-of-slice is a module applicability decision, not necessarily an application
refusal. Preserve existing full interpretations of mixed requests; any delegated
success must explain the whole request. Unknown modifiers, exclusions and extra
effects are not ignorable structural content.

Canonicalize contracts independently of MRS IDs and action order. Incompatible
complete readings require ambiguity. A relevant unresolved alternative cannot
disappear merely because another reading succeeded; record why alternatives were
rejected. Do not claim completeness under truncation. Apply these checks within
the experimental integration, not as an untested global interpreter rewrite.

### 4. Verify independently and measure the contribution

Freeze 24 acceptance cases before induction, grouped by grammatical structure:

- Four positive controls for existing constructions and role preservation.
- Four positive structural variants of taught constructions.
- Eight positive combinations of initial-value attachment and change construction
  absent from calibration, discovery and teacher prompts: four directional and
  four take-away. Teach each component, withhold the particular pairing. Define
  groups using actual MRS evidence during fixture design.
- Eight negative/ambiguity cases covering missing/conflicting roles, spatial
  down/away, extra or excluded effects, and numeric/scope limits. Annotate exact
  acceptable outcomes; any stop is not automatically correct.

Record duplicate/structural-overlap checks without exposing acceptance to
acquisition. Changed numbers alone are parameter checks, not new structural
generalization. Exclude acceptance cases and answers from teacher/selector inputs.
Only call the evaluation blind if authorship/access actually makes it independent;
otherwise call it frozen development validation. Existing sealed sets stay untouched.

Finish discovery/selection, using at most two iterations, before the final
acceptance run. Freeze code, rules and configuration, then compare all three
versions on the same inputs and histories. No retries with semantic changes after
acceptance exposure:

| Version | Purpose |
| --- | --- |
| A: unchanged interpreter | Actual starting point. |
| B: frozen engineering changes, no distilled clauses | Measures ordinary engineering fixes. |
| C: B plus frozen distilled clauses | Measures what the extracted rules add. |

Report C minus B as the **rule-package contribution**, and C minus A as total
pilot improvement. This does not show the teacher outperforms hand-written rules.
Record proposals retained/rejected, manual semantic edits and reviewer effort.
Selection must be reproducible from frozen discovery inputs. Any manual semantic
repair goes back through discovery before the final freeze; no hidden hand-written
repair is attributed to learned rules. Do not add a fourth implementation arm.

Include failures, exact stopping reasons, wrong accepted meanings, structural and
family coverage, clause count and latency. Keep parser failures in end-to-end
denominators; report binding accuracy conditional on usable parses separately.
Resource exhaustion on a supported request is an unsuccessful request. Test number
substitutions, initial-value changes and word order as separate property checks.
cvc5 checks lowering, not English understanding.

Fault-test the oracle with reversed direction, swapped initial/operand, an omitted
requirement and an invented action/state. It must reject each corrupted contract.
Expected arithmetic comes from independent decimal calculations; solver-derived
behavior may be observed but cannot supply its own expected answer.

Choose three complete requests from acceptance when freezing that set: a fractional go-down case,
a take-away case with an initial value, and a go-up case with a negative initial
value. Generate Python and C++ for each through the actual CLI and test their
widgets with independently specified values/results, initial state, repeated
clicks, edited state and absence of unrequested controls. All six outputs must pass;
unsupported/build-failed selections remain failures in the denominator.
The earlier reversed-counter example is a mandatory fault-detection check for
the acceptance oracle.

Acceptance failures end this package version. Later tuning makes those cases
development data and requires newly frozen evaluation for a fresh generalization
claim. Reruns for timings/history retain frozen code and rules.

### 5. Package the knowledge and stop

The deliverable is one small versioned rule package: clauses, vocabulary/guards,
source-example IDs, provenance, verified coverage, rejected hypotheses and a
results manifest. Discovery examples remain evaluation/training artifacts, not
a runtime prompt-to-answer lookup table.

Runtime is request → ACE with native C++ MRS handling → verified symbolic numeric binding → existing
constructor. It needs neither a teacher process nor a teacher response cache.
Start with an opt-in experimental profile; enablement and rollback operate on
one package/version. Promotion is justified by the gates below, not by a teacher's
confidence. A teacher-written code patch alone does not count as completing the
distillation process.

## Pilot gates and effort bounds

- At least 15/16 supported acceptance cases correct, with zero incorrect accepted
  interpretations and all eight negative/ambiguous cases handled appropriately.
  This is a finite empirical gate, not proof that wrong acceptance is impossible.
- Report B's remaining misses and C's recoveries. Require gains on at least two
  structurally distinct cases spanning directional change and take-away, including
  a withheld combination in each. Six recoveries is a stretch target, not a reason
  to call smaller gains worthless. If B leaves insufficient opportunity across
  those families, report insufficient opportunity for the attribution claim;
  do not manufacture harder acceptance cases after seeing scores.
- No regression in previously correct cases of the existing 32-case comparison;
  retain its original incorrect cases and the correct refusal requirements. Detect
  newly introduced wrong acceptances, including outside-slice requests.
- All six preselected Python/C++ outputs pass independent interaction checks;
  all existing CTest suites pass after implementation.
- Within the declared parser budget, equivalent numeric contracts are stable
  across cold/resident/reordered trials. Search exhaustion is explicit; an
  accepted/limited discrepancy is not a consistency pass.
- Target added warm binding p95 overhead at most 50 ms over B. Replay identical
  saved parses to isolate binding, and separately measure live parsing and code
  generation. Record repetitions/hardware/cold startup. Faster binding alone does
  not establish an overall generation speedup.
- Maximum three semantic families, two discovery/selection iterations, and sixty
  teacher requests including calibration, proposals and retries. Record token use
  and review effort as well as call count. Cap the offline selection
  search at sixty seconds per iteration. Do not expand scope to rescue a failed
  pilot or repeatedly tune against acceptance cases.

These are proposed gates, not measured or guaranteed results. Distinguish a
promotable result, useful but insufficient gain, no demonstrated gain, and a
feasibility failure. Retain evidence and attribute limitations to teacher quality,
missing MRS evidence, expressiveness, ambiguity, integration or execution. Do not
move gates after acceptance exposure. A later slice needs its own hypothesis/budget.

## Minimal implementation layout

Proposed capsule: experiments/symbolic_distillation/numeric_change, containing the
scope/schema, split/gold manifests, teacher prompt/configuration, discovery
provenance, bounded selector, verifier and results. Teacher data and draft clauses
are isolated from normal runtime files. Accepted numeric bindings lower through existing
BehaviourSpec builders. The only promoted runtime knowledge is the reviewed,
versioned symbolic module and its small typed adapter.

The [broader symbolic interpretation roadmap](SYMBOLIC_INTERPRETATION_PLAN.md)
remains a reference for later work. This pilot establishes whether targeted
symbolic distillation actually improves unllm before applying the method elsewhere.
