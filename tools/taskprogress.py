#!/usr/bin/env python3
"""Progress for any long task, shown in the KDE system tray by
progress_tray.py and in the terminal by `unllm_ingest.py progress --watch`.

From Python:

    from taskprogress import Task
    t = Task("layer-sweep", "Layer sweep", total=5700)
    t.step(detail="setting 4 of 19")   # one unit done
    t.finish()

From a shell script (the task belongs to the calling shell unless --pid
names another process):

    taskprogress.py start backup "Backup" 120
    taskprogress.py step backup --detail "photos"      # one more done
    taskprogress.py set backup 57                      # or say how many
    taskprogress.py finish backup

Each task is ~/.local/share/unllm/progress/task-<name>.json (pid, title,
done, total, started, detail, and moved: when done last changed). A task
whose process is gone shows as stopped.

The time left is counted from the last step, not from now: the average step
so far, times the steps still to do, after the moment the last one finished.
So it counts down while a step runs, and is not shown once that step has run
past the estimate (a growing guess helps nobody).
"""

import argparse
import glob
import json
import os
import sys
import threading
import time

PROGRESS = os.path.expanduser("~/.local/share/unllm/progress")


def path_of(name):
    return os.path.join(PROGRESS, f"task-{name}.json")


def save(path, state):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(state, f)
    os.replace(tmp, path)


def load(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def alive(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, ValueError, TypeError):
        return False


class Task:
    def __init__(self, name, title, total, done=0, pid=None):
        self.path = path_of(name)
        self.state = {"pid": pid or os.getpid(), "title": title, "done": done, "total": total,
                      "started": time.time(), "start_done": done, "detail": ""}
        self.lock = threading.Lock()
        self.last = 0.0
        os.makedirs(PROGRESS, exist_ok=True)
        save(self.path, self.state)

    def step(self, n=1, detail=None):
        with self.lock:
            self.state["done"] += n
            if n:
                self.state["moved"] = time.time()
            if detail is not None:
                self.state["detail"] = detail
            if time.time() - self.last > 1 or self.state["done"] >= self.state["total"]:
                self.last = time.time()
                save(self.path, self.state)

    def note(self, detail):
        with self.lock:
            self.state["detail"] = detail
            save(self.path, self.state)

    def finish(self):
        try:
            os.remove(self.path)
        except OSError:
            pass


def task_states():
    """Every task file: a dict each with name, title, fraction, done, total,
    left (seconds, when known), detail, and alive (False when the file is
    there but its process has died)."""
    now = time.time()
    out = []
    for path in sorted(glob.glob(os.path.join(PROGRESS, "task-*.json"))):
        t = load(path)
        if not t:
            continue
        total = max(1, int(t.get("total", 1)))
        done = t.get("done", 0)
        did = done - t.get("start_done", 0)
        moved = t.get("moved")
        took = (moved or now) - t.get("started", now)  # the time the steps done so far took
        left = None
        if did > 0 and moved and took > 10:
            left = moved + took * (total - done) / did - now
            left = left if left > 0 else None  # the current step has run past the estimate
        out.append({"name": os.path.basename(path)[5:-5], "title": t.get("title", "?"),
                    "fraction": min(1.0, done / total), "done": done, "total": total, "detail": t.get("detail", ""),
                    "alive": alive(t.get("pid")), "left": left, "pid": t.get("pid")})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("start")
    s.add_argument("name")
    s.add_argument("title")
    s.add_argument("total", type=int)
    s.add_argument("--pid", type=int, help="the process the task belongs to (default: the calling shell)")
    st = sub.add_parser("step")
    st.add_argument("name")
    st.add_argument("n", nargs="?", type=int, default=1)
    st.add_argument("--detail")
    se = sub.add_parser("set")
    se.add_argument("name")
    se.add_argument("done", type=int)
    se.add_argument("--detail")
    f = sub.add_parser("finish")
    f.add_argument("name")
    args = ap.parse_args()
    path = path_of(args.name)
    if args.cmd == "start":
        Task(args.name, args.title, args.total, pid=args.pid or os.getppid())
        return 0
    if args.cmd == "finish":
        try:
            os.remove(path)
        except OSError:
            pass
        return 0
    state = load(path)
    if not state:
        print(f"taskprogress: no task {args.name}", file=sys.stderr)
        return 1
    done = state["done"] + args.n if args.cmd == "step" else args.done
    if done != state["done"]:
        state["moved"] = time.time()
    state["done"] = done
    if args.detail is not None:
        state["detail"] = args.detail
    save(path, state)
    return 0


if __name__ == "__main__":
    sys.exit(main())
