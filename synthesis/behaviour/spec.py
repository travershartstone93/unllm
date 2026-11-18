"""A specification describes state and observable outcomes, not app families."""
from dataclasses import dataclass
import copy
import math
from upgrade.model import Unsupported
from .expressions import parse,walk,NUMBER,TEXT,BOOL,RECORDS

EFFECTS=("load","save","save_as","new","undo","redo","cut","copy","paste","select_all",
         "append_record","remove_record","set_record","start_clock","stop_clock","reset_clock","sample_clock")


def valid_record(value,fields):
    if not isinstance(value,dict) or set(value)!=set(fields):return False
    for key,kind in fields.items():
        item=value[key]
        if kind==TEXT and isinstance(item,str):continue
        if kind==BOOL and type(item) is bool:continue
        if kind==NUMBER and type(item) in (int,float) and math.isfinite(item) and abs(item)<=1e15:continue
        return False
    return True


def identifier(value):
    if not isinstance(value,str) or not value.isascii() or not value.isidentifier() or value.startswith("_") or len(value)>48:
        raise Unsupported("Identifiers must be short ASCII names.")
    return value


def fields_only(data,allowed,context):
    if not isinstance(data,dict) or set(data)-set(allowed):raise Unsupported("Unknown "+context+" fields.")


@dataclass(frozen=True)
class BehaviourSpec:
    value: dict

    @classmethod
    def from_dict(cls,value):
        fields_only(value,("program","schema","title","state","views","actions","events","checks","interpretation"),"behaviour specification")
        data=copy.deepcopy(value)
        if data.get("schema",2)!=2:raise Unsupported("Behaviour specifications use schema 2.")
        # program is a user label; it is never used for dispatch or planning.
        data.setdefault("program","application");data.setdefault("schema",2);data.setdefault("title",data["program"])
        for key in ("program","title"):
            if not isinstance(data[key],str) or not 1<=len(data[key])<=100 or any(ord(c)<32 for c in data[key]):
                raise Unsupported("Invalid application label.")
        state=data.get("state")
        if not isinstance(state,dict) or not 1<=len(state)<=32:raise Unsupported("Provide 1 to 32 typed state fields.")
        for name,field in state.items():
            identifier(name);fields_only(field,("type","initial","fields"),"state")
            kind=field.get("type");initial=field.get("initial")
            valid=(kind==TEXT and isinstance(initial,str) and len(initial)<=1000000) or (kind==BOOL and type(initial) is bool)
            valid=valid or (kind==NUMBER and type(initial) in (int,float) and math.isfinite(initial) and abs(initial)<=1e15)
            if kind==RECORDS:
                schema=field.get("fields")
                if not isinstance(schema,dict) or not 1<=len(schema)<=16:raise Unsupported("A record collection needs typed fields.")
                for key,typ in schema.items():
                    identifier(key)
                    if typ not in (NUMBER,TEXT,BOOL):raise Unsupported("Unsupported record field type.")
                valid=isinstance(initial,list) and len(initial)<=10000 and all(valid_record(v,schema) for v in initial)
            elif "fields" in field:raise Unsupported("Only record collections declare fields.")
            if not valid:raise Unsupported("Invalid initial state: "+name)
        views=data.setdefault("views",[])
        if not isinstance(views,list) or len(views)>48:raise Unsupported("View budget exceeded.")
        ids=set()
        for view in views:
            fields_only(view,("id","kind","state","value","label","selection","display","checked"),"view")
            name=identifier(view.get("id"))
            if name in ids:raise Unsupported("Repeated view identifier.")
            ids.add(name);kind=view.get("kind")
            if kind in ("text_edit","line_edit","number_input"):
                expected=NUMBER if kind=="number_input" else TEXT
                if view.get("state") not in state or state[view["state"]]["type"]!=expected:raise Unsupported("View/state types differ.")
                if "value" in view:raise Unsupported("An input view binds a state, not a display expression.")
            elif kind=="record_list":
                target=view.get("state");selection=view.get("selection")
                if target not in state or state[target]["type"]!=RECORDS:raise Unsupported("A record view needs a collection.")
                if selection not in state or state[selection]["type"]!=NUMBER:raise Unsupported("A record view needs a numeric selection state.")
                if state[target]["fields"].get(view.get("display"))!=TEXT:raise Unsupported("The record display field must be text.")
                if "checked" in view and state[target]["fields"].get(view["checked"])!=BOOL:raise Unsupported("The check field must be Boolean.")
            elif kind=="label":
                if "state" in view or "value" not in view:raise Unsupported("A label needs a value expression.")
                parse(view["value"],state)
            else:raise Unsupported("Unknown widget primitive: "+str(kind))
            if not isinstance(view.get("label",""),str):raise Unsupported("A view label must be text.")
        actions=data.setdefault("actions",[])
        if not isinstance(actions,list) or len(actions)>48:raise Unsupported("Action budget exceeded.")
        names=set();shortcuts=set()
        for action in actions:
            fields_only(action,("id","label","ensure","assign","effect","state","shortcut","values","selection","clear","running","anchor","visible","requires"),"action")
            name=identifier(action.get("id"))
            if name in names or name in ids:raise Unsupported("Repeated action/view identifier.")
            names.add(name)
            if not isinstance(action.get("label",name),str):raise Unsupported("An action label must be text.")
            if type(action.get("visible",True)) is not bool:raise Unsupported("Action visibility must be Boolean.")
            if "requires" in action and parse(action["requires"],state)[1]!=BOOL:raise Unsupported("An action precondition must be Boolean.")
            if sum(key in action for key in ("ensure","assign","effect"))!=1:raise Unsupported("An action needs one outcome contract.")
            shortcut=action.get("shortcut")
            if shortcut is not None:
                if not isinstance(shortcut,str) or not 1<=len(shortcut)<=32 or shortcut in shortcuts:raise Unsupported("Invalid/repeated shortcut.")
                shortcuts.add(shortcut)
            if "ensure" in action:
                tree,kind=parse(action["ensure"],state,transition=True)
                outputs={v.name for v in walk(tree) if v.kind=="next"}
                if kind!=BOOL or not outputs or any(state[n]["type"]!=NUMBER for n in outputs):
                    raise Unsupported("A transition must constrain one or more numeric next states.")
                if any(v.kind=="primitive" or (v.kind in ("prev","next") and state[v.name]["type"]!=NUMBER) for v in walk(tree)):
                    raise Unsupported("Scalar transition synthesis currently uses numeric states.")
                if any(v.kind=="binary" and v.name=="divide" and (v.args[1].kind!="literal" or v.args[1].value==0) for v in walk(tree)):
                    raise Unsupported("Formal transition division needs a nonzero literal denominator.")
                def degree(node):
                    if node.kind=="literal":return 0
                    if node.kind in ("prev","next"):return 1
                    values=[degree(a) for a in node.args]
                    return sum(values) if node.kind=="binary" and node.name=="multiply" else max(values,default=0)
                if degree(tree)>2:raise Unsupported("Transition synthesis supports polynomials through degree two.")
            elif "assign" in action:
                assignments=action["assign"]
                if not isinstance(assignments,dict) or not assignments:raise Unsupported("Provide assignment outcomes.")
                for target,source in assignments.items():
                    value,typ=parse(source,state)
                    if target not in state or typ!=state[target]["type"]:raise Unsupported("Assignment outcome type mismatch.")
                    if typ==RECORDS:
                        def shapes(node):
                            if node.kind=="records":return []
                            if node.kind=="state":return [state[node.name]["fields"]]
                            if node.kind=="conditional":return shapes(node.args[1])+shapes(node.args[2])
                            raise Unsupported("Unknown record-producing expression.")
                        if any(shape!=state[target]["fields"] for shape in shapes(value)):
                            raise Unsupported("Record schemas differ across the assignment.")
            else:
                target=action.get("state")
                effect=action["effect"]
                if effect not in EFFECTS or target not in state:
                    raise Unsupported("Unknown effect or incompatible state.")
                if effect in ("load","save","save_as","new") and state[target]["type"] not in (TEXT,RECORDS):
                    raise Unsupported("Persistence needs text or records.")
                if effect in ("undo","redo","cut","copy","paste","select_all") and not any(v.get("state")==target and v["kind"] in ("text_edit","line_edit") for v in views):
                    raise Unsupported("This edit effect needs an editable view for the state.")
                if effect in ("append_record","remove_record","set_record"):
                    if state[target]["type"]!=RECORDS:raise Unsupported("Collection effects need records.")
                    if (effect!="append_record" or "selection" in action) and (action.get("selection") not in state or state[action["selection"]]["type"]!=NUMBER):
                        raise Unsupported("A collection change needs a numeric selection.")
                    values=action.get("values",{})
                    if not isinstance(values,dict) or set(values)-set(state[target]["fields"]):raise Unsupported("Unknown record fields.")
                    if effect=="append_record" and set(values)!=set(state[target]["fields"]):raise Unsupported("Append must supply every record field.")
                    if effect=="set_record" and not values:raise Unsupported("Provide fields to update.")
                    for key,expr in values.items():
                        if parse(expr,state)[1]!=state[target]["fields"][key]:raise Unsupported("Record field type mismatch.")
                    if any(n not in state or state[n]["type"]!=TEXT for n in action.get("clear",[])):
                        raise Unsupported("Clear targets must be text states.")
                if effect.endswith("_clock"):
                    if state[target]["type"]!=NUMBER or state.get(action.get("anchor"),{}).get("type")!=NUMBER or state.get(action.get("running"),{}).get("type")!=BOOL:
                        raise Unsupported("Clock effects need elapsed/anchor numbers and a running flag.")
        events=data.setdefault("events",[])
        if not isinstance(events,list) or len(events)>8:raise Unsupported("Periodic event budget exceeded.")
        for event in events:
            fields_only(event,("kind","interval_ms","action"),"event")
            if event.get("kind")!="periodic" or type(event.get("interval_ms")) is not int or not 10<=event["interval_ms"]<=60000 or event.get("action") not in names:
                raise Unsupported("Invalid periodic event binding.")
        checks=data.setdefault("checks",[])
        if not isinstance(checks,list) or len(checks)>64:raise Unsupported("Acceptance-check budget exceeded.")
        return cls(data)

    def data(self):return copy.deepcopy(self.value)

    @property
    def state(self):return self.value["state"]

    @property
    def actions(self):return self.value["actions"]

    @property
    def views(self):return self.value["views"]

    def contract(self):
        return {"representation":"typed state, observable views and action postconditions/effects; no application-name dispatch",
                "numbers":"finite binary64, +/-1e15; scalar proof over mathematical reals",
                "text":"Unicode; UTF-8 files limited to 4 MiB; atomic save; cancelled/failed operations preserve state",
                "synthesis":"bounded polynomial-real SyGuS through degree two; prerequisite-based operators for text/records/clock/UI/file effects",
                "records":"typed flat records; at most 10000 items; strict UTF-8 JSON persistence",
                "clock":"monotonic elapsed time; explicit running/anchor state; periodic events; reset stops and zeros the duration",
                "validation":"independent outcome checks and real Qt interactions; finite validation, not unrestricted proof"}
