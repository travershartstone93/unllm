#!/usr/bin/env python3
"""unllm's scripts report progress through the machine's progress helper, which
lives outside this repo at ~/.local/lib/taskprogress (the progress bar rule is
not unllm's: it covers every job on the machine). This file only keeps
`from taskprogress import Task` working for the scripts in tools/ and bench/,
and `python3 tools/taskprogress.py ...` working as the command line.
"""

import importlib.util
import os
import sys

_PATH = os.path.expanduser("~/.local/lib/taskprogress/taskprogress.py")
_spec = importlib.util.spec_from_file_location("taskprogress", _PATH)
_module = importlib.util.module_from_spec(_spec)
sys.modules["taskprogress"] = _module  # the real module takes this name once imported
_spec.loader.exec_module(_module)
globals().update({k: v for k, v in vars(_module).items() if not k.startswith("__")})

if __name__ == "__main__":
    sys.exit(_module.main())
