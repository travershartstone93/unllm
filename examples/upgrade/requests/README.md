# Programs generated from the six original requests

All six English requests from the earlier manual trial now generate working
outputs in both Python/PySide6 and C++/Qt6. The request text is unchanged.
Each output passed ordinary validation and a separate Qt keyboard/mouse/file
workflow. The examples are exported outputs; the generator never reads them.
This is development evaluation within the documented vocabulary and primitives.

Run from the upgrade checkout with a desktop display:

    ./examples/upgrade/requests/run.sh todo python
    ./examples/upgrade/requests/run.sh stopwatch cpp
    ./examples/upgrade/requests/run.sh tip python

Other example names are text_counter, decrement and temperature. Each folder
contains both languages, its typed specification, Python requirements and a
screenshot. The manifest records the original request, source hashes and checks.
The launcher uses the checkout's pinned Python environment, or a local C++ cache.
It rebuilds missing/outdated C++ executables using Clang and Qt6 Widgets.

To regenerate a request and run the independent workflows:

    .venv/bin/python tools/verify_requests.py
    .venv/bin/python tools/export_request_examples.py

Generated programs run without unllm, Prolog, cvc5 or a model. Python needs
its listed packages; C++ needs Qt6 runtime libraries. See the
[repair report](../../../docs/REQUEST_REPAIR.md) for exact outcomes and limits.
