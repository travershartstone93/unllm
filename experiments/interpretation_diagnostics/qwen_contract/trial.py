"""N21 local Ollama paired trial. Run once per arm with the arm name."""
import json
import re
import signal
import sys
import time
from pathlib import Path

import jsonschema
import requests

HERE = Path(__file__).resolve().parent
URL = "http://127.0.0.1:11434/api/chat"
PROMPT = """You are the request interpreter for a coding tool. Translate the user's English request into requirements. Another component will write the code.
The function has two inputs: values (a list of integers), and cutoff (an integer supplied by the caller).
Return exactly one JSON object, no explanation or Markdown.
For a request you understand include all six fields:
status: "ok"
aggregate: "count" or "sum"
collection: the name of the input list
comparison: "gt", "ge", "lt", "le", or "eq"
compare_to: an object identifying the threshold
required_structure: an array of explicitly requested implementation constraints
The comparison describes each list element relative to the threshold. gt means greater than, ge greater than or equal, lt less than, le less than or equal, eq equal.
If the threshold refers to a named function input, use {"input":"<name>"} for compare_to. If the request gives the threshold as a number, use {"literal":<number>}.
Use [] unless the user explicitly requests an implementation constraint. For a required for loop use ["for"]. For an if inside a required for loop use ["for","if"] in outer-to-inner order.
If a necessary choice is unresolved, return only {"status":"clarify","question":"<one specific question>"}.
If the request is clear but this format cannot express it, return only {"status":"unsupported"}."""
CASES = [
    ("Count the values above the cutoff.", "count", "gt"),
    ("How many entries exceed the cutoff?", "count", "gt"),
    ("How many entries meet or exceed the cutoff?", "count", "ge"),
    ("Add up the entries that exceed the cutoff.", "sum", "gt"),
    ("Count the entries for which the cutoff is greater than the entry.", "count", "lt"),
    ("Count the entries greater than five.", "count", "gt"),
    ("Five was yesterday’s cutoff. Count the entries above the cutoff supplied for this run.", "count", "gt"),
    ("Use a for loop with an if inside it to count entries below the cutoff.", "count", "lt"),
    ("Count the entries on the side of the cutoff I want.", "clarify", None),
    ("Return the entries above the cutoff in their original order.", "unsupported", None),
]
def obj(**fields):
    return dict(type="object", properties=fields, required=list(fields), additionalProperties=False)
def enum(*values):
    return dict(type="string", enum=list(values))
SCHEMA = dict(oneOf=[
    obj(status=enum("ok"), aggregate=enum("count","sum"), collection=enum("values"),
        comparison=enum("gt","ge","lt","le","eq"),
        compare_to=dict(oneOf=[obj(input=enum("cutoff")), obj(literal=dict(type="integer"))]),
        required_structure=dict(type="array",items=enum("for","if"),maxItems=2)),
    obj(status=enum("clarify"),question=dict(type="string",minLength=1)),
    obj(status=enum("unsupported"))
])

class Cancel(Exception):
    pass
def deadline(*args):
    raise Cancel("wall-clock budget")

def run(model, constrained, index, prompt, aggregate, comparison):
    payload=dict(model=model, messages=[dict(role="user",content=PROMPT+"\nUser request: "+prompt)],
                 stream=True, think=False, options=dict(temperature=0,seed=42,num_predict=256,num_ctx=4096))
    if constrained:
        payload["format"]=SCHEMA
    start=time.monotonic()
    row=dict(case=index,prompt=prompt,content="",valid=False,correct=False)
    thought=""
    signal.alarm(45)
    try:
        with requests.post(URL,json=payload,stream=True,timeout=(5,45)) as r:
            r.raise_for_status()
            for line in r.iter_lines(chunk_size=1):
                event=json.loads(line)
                if "error" in event:
                    raise ValueError(event["error"])
                msg=event.get("message",{})
                row["content"]+=msg.get("content","")
                thought+=msg.get("thinking","")
                prefix=row["content"].lstrip()
                if prefix and not prefix.startswith("{"):
                    raise Cancel("answer began outside JSON object")
                stream=thought+row["content"]
                if stream.lower().count("but wait")>=3 or len(thought)>900 or re.search(r"(.{45,}?)\1\1",stream,re.S):
                    raise Cancel("repetition or thinking budget")
                if event.get("done"):
                    row["finish"]=event.get("done_reason")
                    row["tokens"]=event.get("eval_count")
                    break
        answer=json.loads(row["content"])
        jsonschema.validate(answer,SCHEMA)
        row["valid"]=True
        if index==9:
            row["correct"]=answer["status"]=="clarify" and bool(re.search(r"side|above|below|greater|less|higher|lower",answer.get("question",""),re.I))
        elif index==10:
            row["correct"]=answer=={"status":"unsupported"}
        else:
            row["correct"]=answer==dict(status="ok",aggregate=aggregate,collection="values",comparison=comparison,
                compare_to={"literal":5} if index==6 else {"input":"cutoff"},required_structure=["for","if"] if index==8 else [])
    except (Cancel,ValueError,requests.RequestException,jsonschema.ValidationError) as e:
        row["error"]=str(e).splitlines()[0]
    finally:
        signal.alarm(0)
    row["thinking_chars"]=len(thought)
    row["seconds"]=round(time.monotonic()-start,3)
    return row

if __name__=="__main__":
    arm=sys.argv[1]
    model,constrained={"4b-prose":("qwen3:4b",False),"4b-schema":("qwen3:4b",True),"1.7b-schema":("qwen3:1.7b",True)}[arm]
    signal.signal(signal.SIGALRM,deadline)
    requests.post("http://127.0.0.1:11434/api/generate",json=dict(model=model,keep_alive="10m",stream=False),timeout=90).raise_for_status()
    with (HERE/(arm+".jsonl")).open("w") as out:
        for i,args in enumerate(CASES,1):
            row=run(model,constrained,i,*args)
            print(json.dumps(row),file=out,flush=True)
            print(arm,i,row["correct"],row["seconds"],row.get("error",""),flush=True)
