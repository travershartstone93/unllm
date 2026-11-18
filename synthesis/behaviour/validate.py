"""Independent outcome checks and generic Qt interactions in bubblewrap."""
from pathlib import Path
import copy
import json
import math
import shutil
import subprocess
import time
from hypothesis import given,settings,strategies as st
from synthesis.apps.validate import sandbox
from .expressions import parse,evaluate,walk

ROOT=Path(__file__).parent
SAMPLE="Alpha café\nbeta Alpha\n"


def literal(value):return json.dumps(value,ensure_ascii=False)


def observation_header(spec):
    lines=["QJsonObject observe_state(ApplicationWindow& window) {","QJsonObject result;"]
    for name,field in spec.state.items():
        value="window.state_"+name
        if field["type"]=="text":value="qtext("+value+")"
        lines.append("result["+literal(name)+"]="+value+";")
    lines+=["return result;","}","void initialize_state(ApplicationWindow& window,const QJsonObject& initial) {"]
    for name,field in spec.state.items():
        key=literal(name)
        method={"number":"toDouble","bool":"toBool","text":"toString","records":"toArray"}[field["type"]]
        value="initial["+key+"]."+method+"()"
        if field["type"]=="text":value="bytes("+value+")"
        lines.append("if(initial.contains("+key+"))window.state_"+name+"="+value+";")
    return "\n".join([*lines,"}"])+"\n"


def cpp_command(job,filename,sanitized=False,driver=False):
    flags=subprocess.check_output(["pkg-config","--cflags","--libs","Qt6Widgets",*(["Qt6Test"] if driver else [])],text=True).split()
    return ["clang++","-std=c++20","-Wall","-Wextra","-Wno-unused-function",
        *(["-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer"] if sanitized else ["-O2"]),
        str(job/("gui_driver.cpp" if driver else "program.cpp")),*flags,"-o",str(job/filename)]


def scenarios(spec,job,output):
    initial={n:f["initial"] for n,f in spec.state.items()}
    def sample_value(name):
        field=spec.state[name]
        if field["type"]=="text":return SAMPLE
        return [{key:("Alpha café" if kind=="text" else False if kind=="bool" else 1.25) for key,kind in field["fields"].items()}]
    def encoding(name,value):
        return value if spec.state[name]["type"]=="text" else json.dumps(value,ensure_ascii=False)
    def two_records(name):return [copy.deepcopy(sample_value(name)[0]) for _ in range(2)]
    rows=[{"name":"initial","events":[],"expect":{"state":initial}}]
    for view in spec.views:
        if view["kind"] in ("text_edit","line_edit"):
            sample=SAMPLE if view["kind"]=="text_edit" else "Alpha café"
            rows.append({"name":"input-"+view["id"],"events":[{"input":view["id"],"text":sample}],
                         "expect":{"state":{**initial,view["state"]:sample}}})
        elif view["kind"]=="number_input":
            for text,value in (("37.25",37.25),("0.25",.25),("-100.75",-100.75),("1.",1.0)):
                rows.append({"name":"typed-"+view["id"]+"-"+text,
                             "events":[{"number":view["id"],"value":value,"text":text}],
                             "expect":{"state":{**initial,view["state"]:value}}})
        elif view["kind"]=="record_list":
            records=two_records(view["state"])
            rows.append({"name":"select-"+view["id"],"initial":{view["state"]:records},
                         "events":[{"select":view["id"],"row":1}],"expect":{"state":{view["selection"]:1.0}}})
            if view.get("checked"):
                changed=copy.deepcopy(records);changed[0][view["checked"]]=True
                rows.append({"name":"check-"+view["id"],"initial":{view["state"]:records},
                             "events":[{"check":view["id"],"row":0,"value":True}],
                             "expect":{"state":{view["state"]:changed,view["selection"]:0.0}}})
    numbers=[n for n,f in spec.state.items() if f["type"]=="number"]
    samples=[]
    @settings(max_examples=20,derandomize=True,database=None,deadline=None)
    @given(st.lists(st.integers(-1000,1000),min_size=len(numbers),max_size=len(numbers)))
    def generate(values):samples.append(dict(zip(numbers,map(float,values))))
    generate()
    for action in spec.actions:
        if "ensure" in action:
            for index,values in enumerate(samples):
                row={"name":action["id"]+"-"+str(index),"initial":values,"events":[{"button":action["id"]}]}
                previous={**initial,**values}
                if "requires" in action and not evaluate(parse(action["requires"],spec.state)[0],previous):
                    row["expect"]={"state":previous,"has_error":True}
                else:row["postcondition"]=action["ensure"]
                rows.append(row)
        elif "assign" in action:
            seeded={**initial,**{n:SAMPLE for n,f in spec.state.items() if f["type"]=="text"}}
            try:
                if "requires" in action and not evaluate(parse(action["requires"],spec.state)[0],seeded):raise ValueError("Precondition")
                updates={n:evaluate(parse(source,spec.state)[0],seeded) for n,source in action["assign"].items()}
                expected={"state":{**seeded,**updates},"error":""}
            except (ValueError,ZeroDivisionError,OverflowError):
                expected={"state":seeded,"has_error":True}
            rows.append({"name":action["id"],"initial":seeded,"events":[{"button":action["id"]}],
                         "expect":expected})
        elif action["effect"] in ("save","save_as"):
            target=action["state"];destination="/output/save-"+action["id"]+".txt"
            content=sample_value(target);seeded={**initial,target:content}
            rows.append({"name":"persist-"+action["id"],"initial":seeded,
                         "dialogs":{"save":[destination]},"events":[{"button":action["id"]}],
                         "expect":{"state":seeded,"error":"","paths":{target:destination},"dirty":False,
                                   "files":{destination:content}}})
            rows.append({"name":"cancel-"+action["id"],"initial":seeded,"dialogs":{"save":[""]},
                         "events":[{"button":action["id"]}],"expect":{"state":seeded,"error":""}})
            rows.append({"name":"failed-"+action["id"],"initial":seeded,"dialogs":{"save":["/output/missing-directory/file.txt"]},
                         "events":[{"button":action["id"]}],"expect":{"state":seeded,"has_error":True,"dirty":True}})
        elif action["effect"]=="load":
            target=action["state"]
            content=sample_value(target)
            good=job/("fixture-"+action["id"]+".txt");good.write_text(encoding(target,content),encoding="utf-8")
            bad=job/("invalid-"+action["id"]+".txt");bad.write_bytes(b"\xff\xfe\xff")
            for name,path,expect in (
                ("open","/job/"+good.name,{"state":{**initial,target:content},"dirty":False,"error":""}),
                ("cancel","",{"state":initial,"error":""}),
                ("invalid","/job/"+bad.name,{"state":initial,"has_error":True})):
                rows.append({"name":name+"-"+action["id"],"dialogs":{"open":[path]},
                             "events":[{"button":action["id"]}],"expect":expect})
        elif action["effect"] in ("append_record","remove_record","set_record"):
            target=action["state"];effect=action["effect"]
            seeded={**initial,**{n:SAMPLE for n,f in spec.state.items() if f["type"]=="text"}}
            if effect!="append_record":
                seeded[target]=two_records(target);seeded[action["selection"]]=0.0
            expected=copy.deepcopy(seeded)
            values={n:evaluate(parse(expr,spec.state)[0],seeded) for n,expr in action.get("values",{}).items()}
            if effect=="append_record":
                expected[target].append(values)
                if action.get("selection"):expected[action["selection"]]=float(len(expected[target])-1)
                for field in action.get("clear",[]):expected[field]=""
            elif effect=="remove_record":
                expected[target].pop(0);expected[action["selection"]]=0.0
            else:expected[target][0].update(values)
            rows.append({"name":effect+"-"+action["id"],"initial":seeded,"events":[{"button":action["id"]}],
                         "expect":{"state":expected,"error":""}})
        elif action["effect"]=="new":
            target=action["state"];seeded={**initial,target:sample_value(target)}
            rows.append({"name":"new-"+action["id"],"initial":seeded,"dialogs":{"confirm":["discard"]},
                         "events":[{"button":action["id"]}],
                         "expect":{"state":{**initial,target:"" if spec.state[target]["type"]=="text" else []},"dirty":False}})
    # Cancellation is checked for close on any state that participates in
    # persistence, independently of its label or widget name.
    persistent={a["state"] for a in spec.actions if a.get("effect") in ("load","save","save_as","new")}
    for name in persistent:
        rows.append({"name":"close-cancel-"+name,"initial":{name:sample_value(name)},
                     "dialogs":{"confirm":["cancel"]},"events":[{"close":True}],
                     "expect":{"visible":True,"dirty":True}})
        rows.append({"name":"close-discard-"+name,"initial":{name:sample_value(name)},
                     "dialogs":{"confirm":["discard"]},"events":[{"close":True}],
                     "expect":{"visible":False}})
    for start in (a for a in spec.actions if a.get("effect")=="start_clock"):
        target=start["state"];running=start["running"]
        stop=next((a for a in spec.actions if a.get("effect")=="stop_clock" and a["state"]==target),None)
        reset=next((a for a in spec.actions if a.get("effect")=="reset_clock" and a["state"]==target),None)
        if not stop or not reset:continue
        rows.append({"name":"clock-start-stop-reset-"+target,"trace":True,
                     "events":[{"button":start["id"]},{"wait_ms":100},{"button":stop["id"]},{"wait_ms":80},{"button":reset["id"]}],
                     "expect":{"state":{target:0.0,running:False},
                               "trace_ranges":[{"at":1,"state":target,"minimum":.02,"maximum":5.0}],
                               "unchanged_between":[[2,3,target]]}})
    rows.extend(copy.deepcopy(spec.value["checks"]))
    if len(rows)>192:raise ValueError("Executable validation scenario budget exceeded")
    return rows


def close_enough(actual,expected):
    if type(expected) is bool:return type(actual) is bool and actual==expected
    if isinstance(expected,list):return isinstance(actual,list) and len(actual)==len(expected) and all(close_enough(a,b) for a,b in zip(actual,expected))
    if isinstance(expected,dict):return isinstance(actual,dict) and set(actual)==set(expected) and all(close_enough(actual[k],v) for k,v in expected.items())
    if type(expected) in (int,float):return type(actual) in (int,float) and math.isfinite(actual) and math.isclose(actual,expected,rel_tol=1e-10,abs_tol=1e-9)
    return actual==expected


def holds(value,previous,current):
    if value.kind=="binary" and value.name in ("and","or"):
        a=holds(value.args[0],previous,current);b=holds(value.args[1],previous,current)
        return a and b if value.name=="and" else a or b
    if value.kind=="binary" and value.name in ("eq","ne"):
        args=[evaluate(v,{"prev":previous,"next":current}) for v in value.args]
        answer=close_enough(args[0],args[1])
        return answer if value.name=="eq" else not answer
    return bool(evaluate(value,{"prev":previous,"next":current}))


def check(spec,scenario,actual,output):
    expected=scenario.get("expect",{})
    for name,value in expected.get("state",{}).items():
        if not close_enough(actual["state"].get(name),value):return "State outcome mismatch: "+name
    if "postcondition" in scenario:
        previous={n:f["initial"] for n,f in spec.state.items()};previous.update(scenario.get("initial",{}))
        post,_=parse(scenario["postcondition"],spec.state,transition=True)
        if actual["error"] or not holds(post,previous,actual["state"]):return "State-transition postcondition failed"
        modified={v.name for v in walk(post) if v.kind=="next"}
        if any(not close_enough(actual["state"][n],v) for n,v in previous.items() if n not in modified):return "Unmentioned state changed"
    for name,value in expected.get("labels",{}).items():
        if actual["labels"].get(name)!=value:return "Display outcome mismatch: "+name
    for key in ("error","dirty","visible"):
        if key in expected and actual[key]!=expected[key]:return "Outcome mismatch: "+key
    if expected.get("has_error") and not actual["error"]:return "Expected an error"
    for name,path in expected.get("paths",{}).items():
        if actual["paths"].get(name)!=path:return "File binding mismatch"
    for path,text in expected.get("files",{}).items():
        filename=Path(path)
        if filename.parent!=Path("/output"):return "Acceptance files must be within /output"
        try:content=(output/filename.name).read_text(encoding="utf-8")
        except (OSError,UnicodeError):return "Expected output file missing or invalid"
        if isinstance(text,(list,dict)):
            try:content=json.loads(content)
            except ValueError:return "Saved JSON is invalid"
        if not close_enough(content,text):return "Saved contents differ"
    for item in expected.get("trace_ranges",[]):
        value=actual["trace"][item["at"]][item["state"]]
        if not item["minimum"]<=value<=item["maximum"]:return "Timed observation outside bounds"
    for before,after,name in expected.get("unchanged_between",[]):
        if actual["trace"][before][name]!=actual["trace"][after][name]:return "Stopped state continued changing"
    # All derived labels are checked against the independent interpreter,
    # regardless of which action or state combination produced the result.
    for view in spec.views:
        if view["kind"]!="label":continue
        try:
            value=evaluate(parse(view["value"],spec.state)[0],actual["state"])
            text=format(0.0 if value==0 else value,".12g") if type(value) in (int,float) else str(value).lower() if type(value) is bool else value
            wanted=(view.get("label","")+": " if view.get("label") else "")+text
        except (ValueError,ZeroDivisionError,OverflowError):wanted="Error"
        if actual["labels"].get(view["id"])!=wanted:return "Derived view differs: "+view["id"]
    return None


def run_cases(spec,job,language,cases,deadline,preview=False):
    commands=[(["/runtime/python/bin/python3.12","/job/gui_driver.py"],False,True)] if language=="python" else [
        (["/job/gui_driver"],False,False),(["/job/gui_driver.checked"],True,False)]
    observations=[];count=0
    for command,sanitized,python in commands:
        output=job/("checked-output" if sanitized else "test-output");output.mkdir(exist_ok=True)
        result=sandbox(job,command,{"scenarios":cases,"preview":preview},output,sanitized,
                       max(.1,min(30,deadline-time.monotonic())),python)
        if result.returncode:return {"valid":False,"stage":"execution","returncode":result.returncode,
                                    "stderr":result.stderr[-8000:],"stdout":result.stdout[-2000:]}
        actual=json.loads(result.stdout);observations.append(actual)
        if len(actual)!=len(cases):raise ValueError("Incomplete validation response")
        for scenario,observed in zip(cases,actual):
            count+=1;error=check(spec,scenario,observed,output)
            if error:return {"valid":False,"stage":"outcome","scenario":scenario,"actual":observed,"message":error}
    return {"valid":True,"cases":count,"gui_scenarios":len(cases),"sanitizers":language=="cpp",
            "observations":observations,"method":"independent specification interpreter; Hypothesis states; Qt mouse/edit/file events",
            "preview":str(job/"test-output/preview.png") if preview else None}


def validate(spec,job,language,deadline):
    compilations=[]
    if language=="python":shutil.copyfile(ROOT/"python_driver.py",job/"gui_driver.py")
    else:
        shutil.copyfile(ROOT/"cpp_driver.cpp",job/"gui_driver.cpp")
        (job/"observation.h").write_text(observation_header(spec))
        for filename,sanitized,driver in (("program",False,False),("gui_driver",False,True),("gui_driver.checked",True,True)):
            command=cpp_command(job,filename,sanitized,driver)
            result=subprocess.run(command,text=True,capture_output=True,timeout=max(.1,min(50,deadline-time.monotonic())))
            compilations.append({"command":command,"returncode":result.returncode,"diagnostics":result.stderr[-6000:]})
            if result.returncode:return {"valid":False,"stage":"compile","compilations":compilations}
    output=job/"test-output";output.mkdir(exist_ok=True)
    cases=scenarios(spec,job,output)
    (job/"validation-cases.json").write_text(json.dumps(cases,indent=2,ensure_ascii=False)+"\n")
    result=run_cases(spec,job,language,cases,deadline,True)
    result["compilations"]=compilations
    result["sandbox"]="bubblewrap; no network; read-only job and runtimes; isolated writable output"
    return result
