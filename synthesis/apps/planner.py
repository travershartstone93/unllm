"""Recursive native Prolog planning, Souffle wiring and independent checks."""
from upgrade.model import Claim,Goal,Relation,Unsupported,digest
from upgrade.proof import ProofChecker


def catalog():
    entries=[]
    def add(name,capability,requires=(),target="any",accepts=(),produces=()):
        entries.append({"id":"app-component:"+name,"capability":capability,"requires":list(requires),
                        "target":target,"accepts":list(accepts),"produces":list(produces)})
    add("interactive_expression","app:interactive_expression",
        ("expression:parse","expression:evaluate","state:editable","events:dispatch","ui:window","ui:display","ui:grid"))
    add("antlr4","expression:parse",accepts=("text",),produces=("expression_tree",))
    add("checked_arithmetic","expression:evaluate",("error:recover",),accepts=("expression_tree",),produces=("number",))
    add("editable_state","state:editable",accepts=("action",),produces=("application_state",))
    add("dispatch","events:dispatch",accepts=("ui_event",),produces=("action",))
    add("recover","error:recover")
    for target in ("python","cpp"):
        add("qt_window_"+target,"ui:window",target=target,produces=("ui_event",))
        add("qt_display_"+target,"ui:display",target=target,accepts=("application_state",))
        add("qt_grid_"+target,"ui:grid",target=target)
    for op in ("add","subtract","multiply","divide"):
        add(op,"arithmetic:"+op,("expression:evaluate",))
    for feature in ("parentheses","backspace","keyboard"):
        add(feature,"control:"+feature,("state:editable","events:dispatch"))
    add("memory","control:memory",("state:numeric_memory","arithmetic:add","arithmetic:subtract"))
    add("numeric_memory","state:numeric_memory",("state:editable","error:recover"))
    claims=[]
    for item in entries:
        rows=[("implements",item["capability"]),("target",item["target"]),("available","true")]
        rows += [("requires_capability",v) for v in item["requires"]]
        rows += [("accepts",v) for v in item["accepts"]]+[("produces",v) for v in item["produces"]]
        for p,o in rows:
            claims.append(Claim("app-contract:"+digest([item["id"],p,o]),p,item["id"],o,
                                source="application-components-v1",origin="metadata"))
    relations=[Relation(p).data() for p in ("implements","target","available","requires_capability","accepts","produces","compatible")]
    return entries,claims,relations


def construct_plan(spec,native,language,disabled=()):
    entries,claims,relations=catalog()
    if disabled:claims=[c for c in claims if not(c.predicate=="available" and c.subject in disabled)]
    requirements=["app:interactive_expression",*["arithmetic:"+op for op in spec.operations]]
    requirements += ["control:"+name for name in ("memory","parentheses","backspace","keyboard") if getattr(spec,name)]
    response=native.application_plan([c.data() for c in claims],relations,requirements,language)
    if response["status"]!="complete":raise Unsupported("No complete application construction satisfies the requested capabilities.")
    checked=ProofChecker(claims,relations,[]).check(response["proofs"])
    by_id={e["id"]:e for e in entries}
    available={c.subject for c in claims if c.predicate=="available" and not c.negative}
    chosen={};count=0
    def verify(tree,wanted,seen=()):
        nonlocal count
        count+=1
        if count>256 or wanted in seen or tree.get("functor")!="node":raise ValueError("Invalid application plan graph")
        cap,component,children=tree["args"]
        entry=by_id.get(component)
        if cap!=wanted or not entry or entry["capability"]!=cap or entry["target"] not in ("any",language) or component not in available:
            raise ValueError("An application provider does not satisfy its obligation")
        if sorted(t["args"][0] for t in children)!=sorted(entry["requires"]):raise ValueError("An application dependency is missing")
        if cap in chosen and chosen[cap]!=component:raise ValueError("Inconsistent provider selection")
        chosen[cap]=component
        for child in children:verify(child,child["args"][0],(*seen,wanted))
    if len(response["trees"])!=len(requirements):raise ValueError("An application requirement is missing")
    for tree,required in zip(response["trees"],requirements):verify(tree,required)
    wires=[("expression:parse","expression:evaluate"),("ui:window","events:dispatch"),
           ("events:dispatch","state:editable"),("state:editable","ui:display")]
    goals=[Goal("compatible",chosen[a],chosen[b]) for a,b in wires]
    connections=native.query([c.data() for c in claims],relations,[],[g.data() for g in goals])
    if not connections["solutions"]:raise Unsupported("The selected application components have incompatible interfaces.")
    row=connections["solutions"][0]
    wire_proof=ProofChecker(claims,relations,[]).check(row["proofs"],goals,row["bindings"])
    return {"requirements":requirements,"providers":chosen,"components":sorted(set(chosen.values())),
            "trees":response["trees"],"derivation":checked,"wires":[g.data() for g in goals],
            "wire_derivation":wire_proof,"engines":["SWI-Prolog recursive provider search","Souffle compatibility"]}
