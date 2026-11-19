"""Replay N21 through standalone OpenCode and score the assistant's raw text."""
import json
import re
import subprocess
import tempfile
import time

from trial import CASES, HERE, PROMPT, SCHEMA, jsonschema


def invoke(prompt):
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as cwd:
        cmd = ["timeout", "-k", "2", "45", "opencode", "run", "--standalone",
               "--model", "opencode/big-pickle", "--format", "json",
               PROMPT + "\nUser request: " + prompt]
        with subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, text=True) as proc:
            events = []
            for line in proc.stdout:
                event = json.loads(line)
                events.append(event)
                part = event.get("part", {})
                text = part.get("text", "")
                if (event["type"] in ("tool_use", "error") or
                    (event["type"] == "text" and text.strip() and not text.lstrip().startswith("{")) or
                    text.lower().count("but wait") >= 3):
                    proc.terminate()
                    break
            rc = proc.wait()
    return events, rc, time.monotonic() - start


def main():
    first = HERE / "big-pickle-first.tmp"
    with (HERE / "big-pickle.jsonl").open("w") as out:
        for i, (prompt, aggregate, comparison) in enumerate(CASES, 1):
            if i == 1 and first.exists():
                raw = json.loads(first.read_text())
                events = [json.loads(line) for line in raw["stdout"].splitlines()]
                rc, seconds = raw["rc"], raw["seconds"]
            else:
                events, rc, seconds = invoke(prompt)
            content = "".join(e["part"]["text"] for e in events if e["type"] == "text")
            row = dict(case=i,prompt=prompt,content=content,seconds=round(seconds,3),
                       exit_code=rc,valid=False,correct=False)
            try:
                answer = json.loads(content)
                jsonschema.validate(answer, SCHEMA)
                row["valid"] = True
                if i == 9:
                    row["correct"] = answer["status"] == "clarify" and bool(re.search(
                        r"side|above|below|greater|less|higher|lower", answer.get("question",""), re.I))
                elif i == 10:
                    row["correct"] = answer == {"status":"unsupported"}
                else:
                    row["correct"] = answer == dict(status="ok",aggregate=aggregate,collection="values",
                        comparison=comparison,compare_to={"literal":5} if i==6 else {"input":"cutoff"},
                        required_structure=["for","if"] if i==8 else [])
            except (ValueError,jsonschema.ValidationError) as ex:
                row["error"] = str(ex).splitlines()[0]
            print(json.dumps(row), file=out, flush=True)
            print(i, rc, row["correct"], row["seconds"],flush=True)

if __name__ == "__main__":
    main()
