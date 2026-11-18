"""A provider catalog for primitive capabilities, without named app recipes."""
from upgrade.model import Claim,Goal,Relation,Unsupported,digest
from upgrade.proof import ProofChecker
from .expressions import parse,walk
from .operators import contract,operators

# Ordered effect procedures are component contracts. Their slots are grounded
# to arbitrary state variables by the constructor. No program names occur here.
DEPENDENCIES={
    "confirm_discard":("choose_existing_or_save","write_atomic_utf8","mark_saved"),
    "choose_existing_or_save":("choose_save",),
    "read_clock":("clock.monotonic",),
}


def plan(spec,native,language,disabled=()):
    entries=[]
    def add(name,requires=(),target="any",accepts=(),produces=(),**metadata):
        entries.append({"id":"behaviour:"+name,"capability":name,"requires":list(requires),"target":target,
                        "accepts":list(accepts),"produces":list(produces),**metadata})
    add("state",produces=("state",));add("dispatch",accepts=("event",),produces=("action",))
    for lang in ("python","cpp"):
        for widget in ("window","text_edit","line_edit","number_input","record_list","label","button"):
            add("ui."+widget+"."+lang,("state","dispatch"),lang,
                accepts=("state",),produces=("event",))
    add("solver.transition",("state",))
    add("assign",("state",))
    add("clock.monotonic",("state",));add("event.periodic",("clock.monotonic","dispatch"))
    for step in sorted(operators()|{d for deps in DEPENDENCIES.values() for d in deps}):
        add(step,DEPENDENCIES.get(step,("state",)))
    action_contracts={}
    for action in spec.actions:
        if "effect" not in action:continue
        effect=action["effect"];tag="action."+action["id"]+"."
        graph,goal=contract(effect);action_contracts[action["id"]]={"graph":graph,"goal":goal}
        if not any(e["capability"]=="effect."+effect for e in entries):add("effect."+effect,("state",))
        for condition,operator in graph.items():
            if operator is None:add(tag+condition,("state",))
            else:
                execute,needs=operator
                add(tag+condition,(*[tag+n for n in needs],execute),
                    execute=execute,action_id=action["id"],condition=condition)
        add(tag+"goal",(tag+goal,"effect."+effect))
    primitive_names=set()
    for view in spec.views:
        if "value" in view:primitive_names.update(v.name for v in walk(parse(view["value"],spec.state)[0]) if v.kind=="primitive")
    for action in spec.actions:
        for source in action.get("assign",{}).values():
            primitive_names.update(v.name for v in walk(parse(source,spec.state)[0]) if v.kind=="primitive")
        for source in action.get("values",{}).values():
            primitive_names.update(v.name for v in walk(parse(source,spec.state)[0]) if v.kind=="primitive")
        if "requires" in action:
            primitive_names.update(v.name for v in walk(parse(action["requires"],spec.state)[0]) if v.kind=="primitive")
    for name in sorted(primitive_names):add("primitive."+name,("state",))
    required=["ui.window."+language]
    required += ["ui."+v["kind"]+"."+language for v in spec.views]
    if spec.actions:required.append("ui.button."+language)
    required += ["solver.transition" if "ensure" in a else "assign" if "assign" in a else "action."+a["id"]+".goal" for a in spec.actions]
    if spec.value.get("events"):required.append("event.periodic")
    required += ["primitive."+n for n in sorted(primitive_names)]
    required=list(dict.fromkeys(required))
    claims=[]
    for entry in entries:
        rows=[("implements",entry["capability"]),("target",entry["target"])]
        if entry["id"] not in disabled:rows.append(("available","true"))
        rows += [("requires_capability",v) for v in entry["requires"]]
        rows += [("accepts",v) for v in entry["accepts"]]+[("produces",v) for v in entry["produces"]]
        claims.extend(Claim("behaviour-contract:"+digest([entry["id"],p,o]),p,entry["id"],o,
                            source="primitive-behaviour-contracts-v2",origin="metadata") for p,o in rows)
    relations=[Relation(p).data() for p in ("implements","target","available","requires_capability","accepts","produces","compatible")]
    result=native.application_plan([c.data() for c in claims],relations,required,language)
    if result["status"]!="complete":raise Unsupported("Available primitive contracts cannot satisfy all requested behaviours.")
    proof=ProofChecker(claims,relations,[]).check(result["proofs"])
    by_id={e["id"]:e for e in entries};selected={};ordered=[];execution={}
    def verify(tree,cap,seen=()):
        if tree.get("functor")!="node" or cap in seen:raise ValueError("Invalid construction plan.")
        wanted,provider,children=tree["args"];entry=by_id.get(provider)
        if wanted!=cap or not entry or entry["capability"]!=cap or provider in disabled or entry["target"] not in ("any",language):
            raise ValueError("Invalid provider selection.")
        if sorted(c["args"][0] for c in children)!=sorted(entry["requires"]):raise ValueError("Missing primitive dependency.")
        for child in children:verify(child,child["args"][0],(*seen,cap))
        if cap not in selected:
            ordered.append(cap)
            if "execute" in entry:execution.setdefault(entry["action_id"],[]).append((entry["condition"],entry["execute"]))
        selected[cap]=provider
    if len(result["trees"])!=len(required):raise ValueError("Missing behaviour obligation.")
    for tree,cap in zip(result["trees"],required):verify(tree,cap)
    # Typed UI state/event wires also cross the existing Souffle compatibility
    # relation; a provider tree alone is insufficient.
    goals=[Goal("compatible",selected["state"],selected["ui.window."+language]),
           Goal("compatible",selected["ui.window."+language],selected["dispatch"])]
    connections=native.query([c.data() for c in claims],relations,[],[g.data() for g in goals])
    if not connections["solutions"]:raise Unsupported("The selected providers have incompatible ports.")
    row=connections["solutions"][0]
    wire_proof=ProofChecker(claims,relations,[]).check(row["proofs"],goals,row["bindings"])
    action_plans={}
    for name,metadata in action_contracts.items():
        available={"state"};steps=[]
        for condition,execute in execution.get(name,[]):
            declared,needs=metadata["graph"][condition]
            if declared!=execute or not set(needs)<=available:raise ValueError("Unsatisfied operator preconditions")
            available.add(condition);steps.append(execute)
        if metadata["goal"] not in available:raise ValueError("Requested effect was not achieved")
        action_plans[name]={"steps":steps,"achieved":sorted(available),"goal":metadata["goal"]}
    return {"requirements":required,"providers":selected,"order":ordered,"trees":result["trees"],
            "derivation":proof,"wire_derivation":wire_proof,"application_name_used":False,"action_plans":action_plans}
