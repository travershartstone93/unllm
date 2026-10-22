#!/usr/bin/env python3
"""Leave-one-out on the verified 50: the default steps with exactly one switched off, per step."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.expanduser("~/repo-work/unllm/tools"))
import retrieval50 as R  # noqa: E402
from taskprogress import Task  # noqa: E402

STEPS = R.OLD.split(",")


def main():
    bar = Task("ablate50", f"Leave-one-out: {len(STEPS)} steps", total=len(STEPS))
    names = []
    for s in STEPS:
        name = f"no-{s}"
        if not os.path.exists(f"{R.AUDIT}/retr-{name}.jsonl"):
            R.run(name, ",".join(x for x in STEPS if x != s))
        names.append(name)
        bar.step(detail=name)
    bar.finish()
    R.score(["base"] + names)


if __name__ == "__main__":
    main()
