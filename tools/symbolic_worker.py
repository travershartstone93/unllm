#!/usr/bin/env python3
"""Persistent JSON-lines endpoint. stdout is exclusively the protocol."""
from pathlib import Path
import json
import os
import signal
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from upgrade.model import Ambiguous, Unsupported


def main():
    database = Path(os.environ.get("UNLLM_FACT_DB", ROOT / ".state/facts.sqlite"))
    engine = Engine(database)
    for line in sys.stdin:
        started = time.monotonic()
        try:
            request = json.loads(line)
            request["_progress"] = lambda value: print(json.dumps(value),flush=True)
            result = engine.ask(request)
        except Ambiguous as exc:
            result = {"status":"ambiguous","text":str(exc)}
        except Unsupported as exc:
            result = {"status":"unsupported","text":str(exc)}
        except Exception as exc:
            result = {"status":"error","text":str(exc),"error_type":type(exc).__name__}
        result["elapsed_ms"] = round((time.monotonic()-started)*1000,3)
        result["parser_ms"] = round(engine.parser.last_ms,3)
        result["build"] = "unllm-upgrade-symbolic-1"
        result["worker"]={"pid":os.getpid(),"logic_library":str(engine.native.library_path),
                          "rule_version":engine.native.rule_version}
        print(json.dumps(result,ensure_ascii=False),flush=True)


if __name__ == "__main__":
    main()
