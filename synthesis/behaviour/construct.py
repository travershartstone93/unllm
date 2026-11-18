"""Construct a typed handler graph from outcomes and selected contracts."""
from .expressions import parse,walk
from .solver import synthesize


def construct(spec,plan):
    actions=[];reports=[]
    persistent=sorted({a["state"] for a in spec.actions if a.get("effect") in ("load","save","save_as","new")})
    for action in spec.actions:
        if "ensure" in action:
            if "solver.transition" not in plan["providers"]:raise ValueError("Missing synthesis provider")
            updates,report=synthesize(action,spec.state);reports.append(report)
            actions.append({"id":action["id"],"updates":updates,"steps":[]})
        elif "assign" in action:
            if "assign" not in plan["providers"]:raise ValueError("Missing assignment provider")
            updates={target:parse(expr,spec.state)[0] for target,expr in action["assign"].items()}
            actions.append({"id":action["id"],"updates":updates,"steps":[]})
        else:
            steps=plan["action_plans"][action["id"]]["steps"]
            if any(step not in plan["providers"] for step in steps):raise ValueError("Unplanned effect instruction")
            actions.append({"id":action["id"],"updates":{},"steps":list(steps),"target":action["state"],
                            "parameters":{k:v for k,v in action.items() if k not in ("id","label","shortcut","visible")}})
    for action,source in zip(actions,spec.actions):
        action["guard"]=parse(source["requires"],spec.state)[0] if "requires" in source else None
    labels={}
    for view in spec.views:
        if view["kind"]=="label":labels[view["id"]]=parse(view["value"],spec.state)
    ir={"state":spec.state,"views":spec.views,"actions":actions,"labels":labels,"persistent":persistent,"events":spec.value.get("events",[])}
    return ir,reports


def data(ir):
    return {"state":ir["state"],"views":ir["views"],"persistent":ir["persistent"],
            "actions":[{**a,"updates":{n:v.data() for n,v in a["updates"].items()},"guard":a["guard"].data() if a["guard"] else None} for a in ir["actions"]],
            "events":ir["events"],
            "labels":{n:{"expression":v.data(),"type":t} for n,(v,t) in ir["labels"].items()}}
