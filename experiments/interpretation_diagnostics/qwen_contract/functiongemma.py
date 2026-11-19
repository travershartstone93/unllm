"""N24 local native tool calling. Run from any directory after ollama pull functiongemma."""
import json
import re
import signal
import time

import jsonschema
import requests

from trial import CASES, HERE, SCHEMA, URL, Cancel, deadline, enum, obj

MODEL = "functiongemma"
SYSTEM = "You are a model that can do function calling with the following functions"


def tool(name, description, parameters):
    return dict(type="function", function=dict(name=name, description=description,
                                               parameters=parameters))


params = obj(aggregate=enum("count", "sum"), collection=enum("values"),
             comparison=enum("gt", "ge", "lt", "le", "eq"),
             required_structure=dict(type="array", items=enum("for", "if")))
params["properties"]["input_threshold"] = dict(type="string", enum=["cutoff"],
    description="Use when the caller supplies the threshold through the cutoff input.")
params["properties"]["literal_threshold"] = dict(type="integer",
    description="Use when the user specifies a fixed threshold number.")
params["properties"]["comparison"]["description"] = (
    "Compare each list element against the threshold: greater, greater or equal, "
    "less, less or equal, or equal, respectively.")
params["properties"]["required_structure"]["description"] = (
    "Only explicitly required constructs, outer to inner. Use [] for unspecified.")
TOOLS = [
    tool("construct_reduction", "Specify a count or sum over the integer list values. "
         "The caller supplies values and integer cutoff. Supply exactly one threshold "
         "argument: input_threshold or literal_threshold.", params),
    tool("ask_clarification", "Ask one specific question when a necessary choice is unresolved.",
         obj(question=dict(type="string", minLength=1))),
    tool("report_unsupported", "The request is clear but cannot be expressed as a count or sum.", obj()),
]
WEATHER = tool("get_current_temperature", "Gets the current temperature for a given location.",
               obj(location=dict(type="string", description="City name.")))


def infer(prompt, tools):
    payload = dict(model=MODEL, tools=tools, stream=True, messages=[
        dict(role="system", content=SYSTEM), dict(role="user", content=prompt)],
        options=dict(temperature=0, seed=42, num_predict=256, num_ctx=4096))
    row = dict(prompt=prompt, calls=[], content="")
    start = time.monotonic()
    signal.alarm(45)
    try:
        with requests.post(URL, json=payload, stream=True, timeout=(5,45)) as response:
            response.raise_for_status()
            for line in response.iter_lines(chunk_size=1):
                event = json.loads(line)
                if "error" in event:
                    raise RuntimeError(event["error"])
                msg = event["message"]
                row["calls"].extend(msg.get("tool_calls", []))
                row["content"] += msg.get("content", "")
                if row["calls"]:
                    row["finish"] = "first_call"
                    break
                if row["content"].lower().count("but wait") >= 3 or len(row["content"]) > 900:
                    raise Cancel("prose budget")
                if event.get("done"):
                    row["finish"] = event.get("done_reason")
                    row["tokens"] = event.get("eval_count")
                    break
    except Cancel as exc:
        row["error"] = str(exc)
    finally:
        signal.alarm(0)
    row["seconds"] = round(time.monotonic()-start, 3)
    return row


def adapt(row):
    if len(row["calls"]) != 1 or row.get("finish") != "first_call":
        raise ValueError("expected a single completed tool call")
    call = row["calls"][0]["function"]
    name, args = call["name"], call["arguments"].copy()
    tools = {t["function"]["name"]: t["function"] for t in TOOLS}
    if name not in tools:
        raise ValueError("unknown tool")
    jsonschema.validate(args, tools[name]["parameters"])
    if name == "ask_clarification":
        return dict(status="clarify", **args)
    if name == "report_unsupported":
        return dict(status="unsupported")
    if ("input_threshold" in args) == ("literal_threshold" in args):
        raise ValueError("expected exactly one threshold")
    threshold = (dict(input=args.pop("input_threshold")) if "input_threshold" in args
                 else dict(literal=args.pop("literal_threshold")))
    return dict(status="ok", compare_to=threshold, **args)


def main():
    signal.signal(signal.SIGALRM, deadline)
    demo = infer("What's the temperature in London?", [WEATHER])
    (HERE/"functiongemma-control.json").write_text(json.dumps(demo, indent=2)+"\n")
    f = demo["calls"][0]["function"]
    assert f["name"] == "get_current_temperature" and f["arguments"]["location"].strip() == "London", demo
    with (HERE/"functiongemma.jsonl").open("w") as out:
        for i, (prompt, aggregate, comparison) in enumerate(CASES, 1):
            row = infer(prompt, TOOLS)
            row.update(case=i, valid=False, correct=False)
            try:
                answer = adapt(row)
                jsonschema.validate(answer, SCHEMA)
                row.update(valid=True, answer=answer)
                if i == 9:
                    row["correct"] = answer["status"] == "clarify" and bool(re.search(
                        r"side|above|below|greater|less|higher|lower", answer.get("question",""), re.I))
                elif i == 10:
                    row["correct"] = answer == {"status":"unsupported"}
                else:
                    row["correct"] = answer == dict(status="ok", aggregate=aggregate,
                        collection="values", comparison=comparison,
                        compare_to={"literal":5} if i==6 else {"input":"cutoff"},
                        required_structure=["for","if"] if i==8 else [])
            except (ValueError,jsonschema.ValidationError) as exc:
                row["error"] = str(exc).splitlines()[0]
            print(json.dumps(row), file=out, flush=True)
            print(i,row["valid"],row["correct"],row["seconds"],flush=True)


if __name__ == "__main__":
    main()
