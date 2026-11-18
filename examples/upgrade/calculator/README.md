# Generated calculator examples

These are exported Python/PySide6 and C++/Qt6 calculator applications. Both variants
support algebraic precedence, decimals, unary signs, parentheses, clear, backspace
and keyboard input. The memory variant adds M+, M-, MR and MC.
The generator never reads this directory as an answer catalog.

From the upgrade checkout:

    ./examples/upgrade/calculator/run-python.sh
    ./examples/upgrade/calculator/run-python.sh memory
    ./examples/upgrade/calculator/run-cpp.sh
    ./examples/upgrade/calculator/run-cpp.sh memory

A desktop display is required. The Python launcher uses the checkout's pinned
virtual environment. The C++ launcher uses an ignored local executable cache and
rebuilds it from these sources when needed, using Qt6 Widgets development files,
Clang and the locally installed ANTLR 4.13.2 static runtime. No unllm worker,
solver or model runs inside either generated application.

To generate another variant:

    ./build/unllm --code python --spec examples/upgrade/calculator/memory.spec.json
    ./build/unllm --code cpp 'Make a standard calculator app.'

Python can also run outside this checkout after installing its exported
requirements.txt. For a separate C++ build, compile program.cpp and Arithmetic*.cpp
with C++20, -DANTLR4CPP_STATIC, Qt6 Widgets and the ANTLR 4.13.2 runtime.
The bundled grammar and ANTLR license accompany each parser.

The manifest records source hashes, specifications, validation jobs and checks.
Each output passed an independent arithmetic oracle and actual Qt mouse/keyboard
interactions; C++ passed normal and address/undefined-behavior sanitizer builds.
Fresh GUI actions were also checked after construction. These are finite
development checks, not a general correctness proof or blind app-generation score.

See [the implementation report](../../../docs/CALCULATOR_UPGRADE.md) for bounds.
Regenerate these exports with .venv/bin/python tools/benchmark_apps.py followed by
.venv/bin/python tools/export_app_examples.py.
