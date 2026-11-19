# Does a coding corpus help unllm synthesize programs?

In a paired test of 16 integer-sequence tasks, extending the symbolic search raised verified solutions from 3 to 11 without a corpus. Giving that search frequency rankings from progressively larger source collections solved the same 11 tasks. When the search could propose only fragments observed in source before falling back to the stock engine, a larger collection raised verified solutions from 6 to 10.

| Search configuration | Passed | Incorrect | Search exhausted | Proposals across suite |
|---|---:|---:|---:|---:|
| Production baseline | 3 | 1 | 12 | 22,450 |
| Extended search, no corpus | 11 | 0 | 5 | 12,297 |
| Extended search, curated rankings | 11 | 0 | 5 | 12,309 |
| Curated plus 512 rows per language | 11 | 0 | 5 | 12,309 |
| Curated plus 8,192 rows per language | 11 | 0 | 5 | 12,315 |
| Curated plus both complete shards | 11 | 0 | 5 | 12,132 |
| Full rankings randomly reassigned | 11 | 0 | 5 | 12,390 |
| Source fragments only, curated | 6 | 0 | 10 | 20,758 |
| Source fragments only, curated plus both shards | 10 | 0 | 6 | 13,158 |

The last two configurations were exploratory. The four additional successes were centered_integral, margin_total, negative_quadratic and positive_headroom. For example, the expanded collection supplied the arithmetic expression for negative_quadratic. The solver supplied the condition and loop. All four also passed with corpus-free search. Four tasks involving reversal, neighbors, alternating signs or prefix spread exhausted every configuration. A fifth, exclusive_energy_debt, was expressible but missed the shortlist.

The production baseline returned one program that fit its eight construction examples and then failed verification. On negative_scan, the input [-3,-3,0] should yield [-3,-6,-6]. The program yielded [-3,-3,-3]. All extended configurations solved that task.

## What was measured

Each task had a fixed typed request, eight construction examples and 32 reserved scoring cases. An independently written oracle cross-checked the expected values. Predictions were frozen before scoring. The native checker evaluated candidates and tested passing programs in both C++ and Python. Three rounds agreed on all decisions, program identities and proposal counts. The normal CLI reproduced the experimental stock arm on all 16 tasks. These are author-designed development tasks.

The extension generated 1,335 distinct arithmetic behaviors and ranked them using the construction examples, with an optional source-frequency tie-break. The reuse experiment instead drew proposals directly from expressions extracted by parsing C++ and Python. Both used the same shortlist limit of 32 and the same underlying search budget. The solver never received source files or complete programs.

The curated index contained 28 expression patterns from 1,176 eligible unique documents. Adding one C++ and one Python shard scanned 328,299 rows and brought that total to 138 patterns across 19,810 documents. The compact expression index grew from 8,364 to 42,723 bytes. The two compressed shards used 811,056,658 bytes. These numbers describe a symbolic index, with no vectorization.

Full rankings saved 165 proposals against corpus-free extended search, about 1.3%. Median online time across the whole suite was 558 ms without rankings and 572 ms with full rankings. The three rounds varied too much to infer a speed benefit. Online timing excludes index construction, one-time solver setup and output verification.

A preliminary run was discarded after finding an incorrect constant in the five-input expression fingerprint. The corrected experiment checked every mined and generated expression fingerprint against its IR and reran all configurations. The verified USB run is under unllm/experiments/corpus-ablation-verified-G4QmHi.

## Decision

Prioritize testing and integrating corpus-free search. Keep the small collection for targeted reuse experiments. Before committing to the remaining download or vectorization, test whether retrieval adds verified solutions on a new task family, such as correct use of an unfamiliar library API. This experiment measured short arithmetic fragments and leaves documentation retrieval, embeddings and broader coding capability open.

See the [original protocol](PROTOCOL.md), [corpus levels](LEVELS.md), [exploratory reuse design](REUSE.md), [per-task outcomes](paired.tsv), [timing and index measurements](metrics.json) and [replay instructions](REPLAY.md).

## State composition on the fixed focused corpus

The opt-in state search builds bounded updates from addition, subtraction, minimum and maximum. It can combine an update with a zero bound, emit an updated state, or count transitions into or out of a sign condition. The index records document frequency and a source location for each observed operation. These are syntactic observations. The search supplies the control flow and validates the resulting program against examples.

On the unchanged 19 development tasks, source-fragment reuse improved from 8 to 10 verified passes. Corpus-ranked synthesis and corpus-free synthesis each improved from 12 to 14. All three gained drawdown and overdraft counting, with identical arithmetic outcomes before and after. Three separate, author-designed transfer probes improved from one pass to three in each configuration. Both emitted languages passed the reserved cases for all reported successes.

The exact 1,855-file expansion was reindexed from its digest manifest. Its previous scalar counts were reproduced and the original scalar representatives retained. Run the experimental trial with a state-enabled index using the prefix `state:reuse:` for direct reuse or `state:` for corpus ranking. Use `state:none` to enumerate the four state operations without retrieval. The previous solver binary, protocol, per-task predictions and checker outputs are in the USB experiment directory `unllm/experiments/state-composition-wAXsxU`.

## Independent acceptance and function retrieval

On nine public HumanEval integer-sequence contracts, the previous corpus-free baseline, state-enabled corpus-free search and state-enabled ranked search each passed positive filtering and list increment. All exhausted the seven remaining searches. Each candidate was checked against public test inputs and 64 additional seeded inputs using the external reference, then checked in both target languages. Three interleaved search rounds agreed. State composition stays opt-in.

The follow-up indexed 6,566 Python and C++ function definitions from the existing 1,855-file manifest. BM25, local BGE-small-en-v1.5 cosine similarity and reciprocal-rank fusion received the same signature, name, path and documentation and a five-candidate budget. Across ten separate public HumanEval descriptions each retrieved a directly usable implementation for four requests and produced four verified programs with an experimental standalone-function adapter. Each also selected one candidate that failed reserved tests. Both private-lookup controls produced zero selections. The adapter binds positional arguments and checks each function in its source language. It does not resolve project dependencies or synthesize argument transformations.

Median warm query times in this small run were 4.2 ms for BM25, 14.4 ms for dense search and 18.9 ms for sequential fusion. The dense matrix used 10,085,504 bytes and the serialized BM25 index 1,075,112 bytes, excluding shared source, identifiers and the encoder. Keep the vector index experimental. All scripts, frozen descriptions, raw ranks, candidate checks, emitted programs and indexes are under the USB experiment directory `unllm/experiments/acceptance-retrieval-sniZsX`.

## Binding an argument from the request

The opt-in C++ target `corpus_argument_bindings` proposes wrappers that insert one bounded integer literal from the request at each positional slot. The retrieval trial tests each original call first, then its wrappers. It selects using construction examples and freezes the winner before checking reserved cases.

With the same function index, ten HumanEval requests and five retrieval slots, dense retrieval improved from four verified programs to five. BM25 and reciprocal-rank fusion each remained at four. Only dense retrieval returned the general modular exponentiation implementation in its top five. Binding the first argument to 2 passed the frozen scoring cases and the original public HumanEval tests. For exponent zero and modulus one, this reused implementation returns 1, matching the public reference implementation; mathematical modular exponentiation returns 0. The previous incorrect string-length selection persisted in all arms, and both private-lookup controls still abstained.

Two supplementary probes reused the frozen modular-exponentiation ranking. A request for 3 raised to an input exponent selected the first argument for binding. A request for an input raised to the third power selected the second. Both passed their scoring cases. These are mechanism checks on the same retrieved function. USB run `unllm/experiments/argument-binding-QIjB7D` holds the frozen protocol, experiment driver, selected programs, rankings by reference and per-call outcomes.

## Fresh-task acceptance for argument binding

The paired run used six previously untested MBPP requests and six previously untested HumanEval requests, plus two private-lookup controls. Selection preceded retrieval. Four requests contained a potential numeric parameter, four used numbers for other purposes and four had no numeric literals. All methods reused the frozen function index, top-five limit and bounded argument proposer. Four construction examples per task selected candidates. The scoring set reserved all published test inputs and further independently checked cases.

BM25, vector and combined retrieval each produced zero verified programs in both direct and bound modes. None selected a wrong program. All abstained on both controls. Across the methods and controls, no call among 161 direct calls and 244 additional bindings passed construction. The 686 reserved public cases were prepared but no selected program advanced to those tests. Keep binding opt-in. This task set does not isolate corpus coverage from retrieval quality.

In a separate diagnostic, vector and combined search ranked the correct largest-prime-factor implementation second and fourth. Its standalone body raised a missing-helper error. Packaging its two original same-file helpers and its original math import produced correct results on all four construction and 77 reserved cases. This diagnostic is outside the acceptance score. Frozen protocols, inputs, ranks, per-call results and diagnostic are on the USB under `unllm/experiments/binding-acceptance-dBuISG`.
