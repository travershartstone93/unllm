# Constructing bounded algorithms from examples

The native CLI accepts an input type, an output type and input-output examples. The request contains no task name, program sketch, loop, or expression. In the prefix-sum trial, the engine constructed a loop that adds each element to a running total and appends that total to the result. The generated programs performed checked signed 64-bit arithmetic. Session validation compiled C++ with ASan/UBSan and ran both target languages in the existing sandbox.

## Use the working CLI

From the upgrade checkout, run:

```sh
contract=experiments/algorithm_ir/evaluation/prefix-example.json
./build/unllm --algorithm synthesize "$contract"
./build/unllm --algorithm construct .state/my-scan "$contract" /usr/bin/python3
```

Use a fresh session directory for the first construction. The first command outputs the candidate, search count, rejected examples and typed representation. The second executes both emitted programs on the supplied examples and publishes the accepted revision in `current.json`. The attempt directory named there contains `algorithm.cpp`, `algorithm.py`, the exact request and the full result. Installed packages expose the same `unllm --algorithm` entry point. Python is needed only to exercise that output language. The ordinary English coding interface retains its existing interpretation scope.

An example contract is `{"input":"ints","output":"int","examples":[{"input":[],"output":"0"},{"input":["2","-3"],"output":"-1"}]}`. Include the desired result on empty input. Integers travel as decimal strings. The existing IR limits sequences to 64 elements and checks signed 64-bit overflow. Many functions can agree on a small set of examples. The search returns the first fitting candidate and records the scope of its validation.

## Search and revision behavior

C++ enumerates typed program fragments in a fixed order. Choices include direct results, mapping or filtering elements, and folds or scans with one integer accumulator. The holes choose an initial value, update expression, optional condition and whether to emit before or after an update. Arithmetic and comparisons combine elements, state and small constants. A failed example becomes an early rejection test for later candidates. Identical candidate trees are skipped. The same implementation serves every task. No model or solver calls are needed for this finite search.

Search has a 120-second and 100,000-proposal ceiling. The bounded grammar can exhaust sooner. The returned statuses distinguish incomplete inputs, unsupported types, inconsistent examples, exhausted search and a candidate. Compilation has a separate 30-second limit and each target execution a 10-second limit.

`revise SESSION CHANGES PYTHON` adds examples, withdraws requirement IDs or replaces their expected outputs. Each new example remains active unless explicitly superseded. The engine checks the prior program first, attempts a single localized typed edit, then can rebuild within the search grammar. It can also apply explicitly requested typed subtree replacements. Each replacement carries a path, the previous component digest and a new value. The transaction requires the current base revision for these edits. The validated IR is authoritative. A session lock serializes writers, attempt directories retain history, and an atomic head update publishes only after both targets pass. A failed attempt leaves its diagnostics and the previous head.

```json
{"examples":[{"input":["2","-4","3"],"output":["2","2","5"]}]}
```

`repair IR CONTRACT` exercises local repair directly on a supplied defective IR. `check IR CONTRACT PYTHON` compares a previously selected IR and both executables against fresh expected values in a separate process.

## Frozen development evaluation

The twelve requests, six stop cases and revision inputs were saved and checksummed before search implementation. Each supported request supplied eight examples. Seven other inputs per task were reserved for scoring and never passed back into the search. The implementing assistant authored the tasks and knew the task families. This is frozen development validation. It does not establish independent or blind generalization.

| Task class | Passed both targets on reserved inputs | Search exhausted |
| --- | --- | --- |
| Sequence transformations | Double each element, negate each element, square each element | Reverse |
| Stateful scans | Running product, exclusive prefix sum, running maximum with a zero floor | Adjacent differences |
| Conditional selection and aggregation | Count negative elements, keep positive elements, sum negative elements, largest value with a zero floor | — |

Ten of twelve passed, covering 70 reserved comparisons per target language. Both misses remained in the denominator. All six incomplete, contradictory and unsupported requests returned their expected status. Ten fitting candidates passed every reserved case. Finite cases leave room for unobserved errors. In particular, the benchmark supplies no overflow oracle for these newly constructed programs. Primitive overflow behavior has separate Stage 14 coverage.

The revision trial accepted four revisions of one program. One preserved the initial structure, one needed a loop, and one wrapped the accumulator update in a conditional. A contradictory fifth request was refused. A separate transaction trial renamed a function through a typed edit, rejected a stale revision and forced a target failure. The last valid head remained byte-identical across both rejections. A subsequent request successfully withdrew one requirement and replaced another.

Five supplementary seeded defects covered arithmetic, conditional behavior, a helper dependency, indexing and collection output. All five failed their initial contract and were repaired on the observed cases, then passed a reserved example in both languages. Four used local edits and one required bounded reconstruction. The dependency and branch cases found behaviorally equivalent fixes at a different site from the injected defect. These author-designed probes were added after the search implementation and supply development evidence only.

The current engine accepts one integer sequence and returns an integer or an integer sequence. It can repair supplied IR containing helpers and indexed loops. Its initial search grammar constructs forward element loops with a single numeric accumulator. Expanding that grammar to discover indexed traversals or previous-element state would address the two measured misses. Automatic helper decomposition, arbitrary repository editing and free-form English algorithm synthesis remain future work.

## Reproduce the measurements

```sh
cmake --build build --target unllm_cli algorithm_search_tests algorithm_ir_tests
ctest --test-dir build --output-on-failure
experiments/algorithm_ir/evaluation/run.sh build/native/unllm_algorithm .state/scan-evaluation /usr/bin/python3
experiments/algorithm_ir/evaluation/repairs.sh build/native/unllm_algorithm .state/scan-repairs /usr/bin/python3
```

Both evaluation scripts require a fresh output directory. The local directory named by `.state/algorithm-search.path` holds the frozen search digest, installed package, per-task source/IR, scoring results, transaction evidence and regression logs. An additional signature check and public CLI packaging were verified by repeating the frozen evaluation on the installed binary. The search grammar and budgets were held fixed.
