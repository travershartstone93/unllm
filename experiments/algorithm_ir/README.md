# Bounded algorithm representation, version 1

This Stage 14 experiment takes a manually supplied algorithm, type-checks it, interprets it in C++ and emits a standalone callable function in C++ or Python. Separate entry points keep language interpretation and algorithm search out of the measurement. The existing public English CLI retains its former behavior.

The [Stage 15 construction experiment](SYNTHESIS.md) now searches this representation directly from typed input-output examples.

## Try it

From the checkout:

```sh
cmake --build build --target unllm_algorithm algorithm_ir_tests
spec=experiments/algorithm_ir/fixtures/prefix.json
build/native/unllm_algorithm run "$spec" '[["2","-3","4"]]'
build/native/unllm_algorithm cpp "$spec"
build/native/unllm_algorithm python "$spec"
ctest --test-dir build -R '^algorithm_ir_cpp$' --output-on-failure
build/native/algorithm_ir_tests "$PWD/build/native/unllm_algorithm" "$PWD/experiments/algorithm_ir/fixtures" /usr/bin/python3
```

The example returns `["2","-1","3"]`. Decimal strings carry signed 64-bit integers across the JSON boundary, including the minimum and maximum values. Generated functions use native integer, Boolean and sequence types. Each emitted module exposes `algorithm(...)`. Python output uses the standard library only. C++ output targets the project's C++20 Clang/GCC toolchain and its checked-arithmetic builtins.

## Version 1 semantics

- A program contains a list of functions. The last is the entry point. Calls can only reference earlier helpers. Parameter and local declarations have explicit types (`int`, `bool`, `ints`). Local initializers can reference preceding bindings.
- Expressions include literals, variable reads, add/subtract/multiply with checked signed 64-bit arithmetic, integer equality and ordering, empty sequences, append, length, checked indexing and typed helper calls. Operands evaluate left to right. Indices start at zero. Integer overflow and invalid indices produce errors in all three implementations.
- Statements include assignment, two-way branches, iteration over the elements of a sequence and iteration over indices from zero up to a supplied bound. Loop variables are scoped to their bodies. Iteration snapshots the input. Append produces a new sequence and caller-owned inputs keep their values. Each function has an explicit final return expression. Empty sequences and zero iterations are valid.
- Each program admits up to four helpers plus the entry, eight parameters and eight locals per function (including loop slots), two nested loops per function, and 80 expression/statement nodes across all functions. Ranges and sequences are limited to 64. A shared budget of 100,000 function entries and loop iterations per invocation also bounds repeated helper work.
- The examples cover prefix sums, a renamed and rescaled variant, conditional aggregation, adjacent differences, sequence snapshotting, indexed lookup, nested ranges, Boolean output and shared-budget exhaustion. A frozen fixture supplies 31 expected results or errors. The driver checks the interpreter, the sanitized compiled C++ executable, the optional Python executable and input preservation. Invalid specifications are rejected before emission.

These are development fixtures with hand-specified algorithm structure and independently enumerated expected results. They measure fidelity across implementations. The roadmap's separate Stage 15 benchmark asks whether the engine can discover that structure from a task contract.
