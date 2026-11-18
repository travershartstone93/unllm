# Generated utility examples

The six English requests from the manual trial, repaired and independently
checked in both languages, are in [the request examples](requests/README.md).

New stateful programs built through the shared constructor are in [the behaviour-contract examples](behaviours/README.md).

Working Python/C++ desktop calculators are in [the calculator examples](calculator/README.md).

These standalone files are exported outputs from the development benchmark, with their typed specifications. The generator never loads them as answer templates. `manifest.json` records source hashes, toolchains, component choices, structure and validation jobs.

| Specification | Python | C++20 | Executable checks (Python / C++) |
| --- | --- | --- | --- |
| [numeric_summary](numeric_summary.spec.json) | [70 lines](numeric_summary.py) | [130 lines](numeric_summary.cpp) | 19 / 38 |
| [record_statistics](record_statistics.spec.json) | [82 lines](record_statistics.py) | [147 lines](record_statistics.cpp) | 16 / 32 |
| [record_means](record_means.spec.json) | [78 lines](record_means.py) | [135 lines](record_means.cpp) | 16 / 32 |

Line counts exclude blank and comment lines; C++ includes its necessary preprocessor directives. Python's concise implementations are intentionally shorter. Common input validation, file handling, errors and reporting are shared component code; the operation pipeline is emitted from the specification.

Numeric summary retains values above one, maps `3*x-2`, sorts descending and reports count/sum/min/max/mean. Record statistics filters values below minus four, maps `3*x+1`, deduplicates, sorts and reports each key; malformed rows are skipped. Record means filters values at ten or above, reverses each group and reports its mean; empty groups are omitted.

```sh
python3 examples/upgrade/numeric_summary.py input.txt
clang++ -std=c++20 -O2 examples/upgrade/record_statistics.cpp -o /tmp/unllm-record-statistics
/tmp/unllm-record-statistics records.tsv
./build/unllm --code python --spec examples/upgrade/record_statistics.spec.json
```

The input/output and numeric contracts are documented in [the upgrade guide](../../docs/UPGRADE.md). The files passed the reported fixed and Hypothesis checks; C++ also passed address/undefined-behavior sanitizer executions. This is finite executable validation, not an unrestricted correctness proof. To regenerate the export, run `tools/benchmark_upgrade.py` and then `tools/export_upgrade_examples.py` with the checkout's virtual-environment Python.
