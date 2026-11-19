# Interval goal construction and candidate selection

The native coding path can construct zero-argument functions over an ascending, inclusive integer interval. Goals specify endpoints, an element transformation (identity or square) and a result operation (collect or sum). The current domain is 1–64 elements with signed 32-bit endpoints and checked signed 64-bit results. The output exposes a callable `algorithm()` in Python or C++.

Try either input through the checkout CLI. Set `UNLLM_PYTHON_TARGET=/usr/bin/python3` to enable executable Python validation.

```sh
build/unllm --json --code cpp 'Make an array from 1-10'
build/unllm --json --code cpp '{"program":"algorithm_goal","interval":[-3,2],"map":"square","reduce":"sum"}'
```

The native interpreter enumerates ACE readings and follows the creation argument, noun arguments, and linked interval endpoints or prepositions. It checks the other predicates against the supported fragment. Equivalent goals share an entry, with the first witness reading and numeric spans recorded. A single distinct candidate proceeds to construction. Competing goals produce an ambiguity response. Empty candidate sets fall through to the existing GUI and stream grounders.

The constructor uses the result type to choose a bounded collection or accumulation loop. Within that loop it tests index/offset and identity/multiplication expressions using the existing algorithm IR interpreter, the requested interval and a nondegenerate control interval. The result operation fixes the loop structure in this slice. Existing emitters generate the selected function. Target validation executes it against the goal's expected result. The evaluation separately compiles/runs the C++ function and calls the Python function against an independent oracle.

## N13 development measurement

The [frozen requests](requests.tsv) contain one known control, nine new supported phrasings, and two stop controls. The author knew the operation families. Of the nine new supported requests, six had the expected candidate, all six were selected, and all six returned the expected result in each target language. The known array succeeded separately; the two stop requests produced no candidate or program. Before this change the C++ English CLI stopped on all twelve.

Every measured reading pool produced zero or one distinct supported goal. Three supported requests had no candidate: the nested array of squares, the starting/ending paraphrase and the between/inclusive paraphrase. The separate parser probe found readings for each. These results measure coverage of this narrow grammar. They provide no comparison of rankers on competing valid goals. Keep ML out of the runtime on this evidence. [Per-request decisions, source hashes and outputs](results.json)

Reproduce from the checkout with a new output path:

```sh
cmake --build build --target unllm_cli algorithm_goal_probe
python3 experiments/algorithm_ir/goal_selection/measure.py .state/goal-selection-replay
```

A separate diagnostic after scoring showed that “Make a number from 1 to 10” could be misread as requesting the whole collection. The corrected grounder requires an explicit collection, transformation or aggregate at the root. The additional probe then produced zero candidates. A full replay preserved the frozen candidate sets, decisions and executed outputs for both languages.
