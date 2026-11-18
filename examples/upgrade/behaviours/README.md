# Programs constructed from behaviour contracts

These Python/PySide6 and C++/Qt outputs use the same construction path.
There is no application-category registration for these programs. Their
specifications declare typed state, observable views and required action outcomes.
The program label is metadata. The examples are never read by the generator.

Run from the upgrade checkout:

    ./examples/upgrade/behaviours/run.sh mixed_workspace python
    ./examples/upgrade/behaviours/run.sh conversion cpp
    ./examples/upgrade/behaviours/run.sh text_workbench python
    ./examples/upgrade/behaviours/run.sh paired_state cpp
    ./examples/upgrade/behaviours/run.sh new_requirements python

Each folder has both source languages, its specification and a screenshot.
The mixed workspace combines editing, derived word counts, a numeric transition,
Boolean state, a text transformation and saving. Conversion and paired state
require relational scalar synthesis, including simultaneous updates.
The final composition's label and coefficients were chosen when the benchmark ran.

The launcher needs a desktop display and the checkout's pinned Python environment
or host Qt6 runtime. C++ can rebuild from the exported source using Clang and Qt6
Widgets development files. Python can run separately with its requirements.txt.
Neither generated language needs unllm, Prolog, cvc5 or a model at application runtime.

Regenerate from a specification:

    ./build/unllm --code python --spec examples/upgrade/behaviours/mixed_workspace/specification.json

The manifest records source hashes, solver results, structure and executable
checks. All outputs passed the ordinary validation and additional fresh inputs;
C++ passed normal and sanitizer executions. This is development evidence of
bounded composition, not unrestricted programming ability or a blind evaluation.
See [the architecture report](../../../docs/BEHAVIOUR_CONSTRUCTION.md).

Reproduce with .venv/bin/python tools/benchmark_behaviours.py followed by
.venv/bin/python tools/export_behaviour_examples.py.
