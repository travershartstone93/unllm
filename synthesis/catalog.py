"""Contracts describe reusable operations, never complete prompt answers."""
from upgrade.model import Claim, Goal, Relation, digest


def catalog():
    components=[]
    def add(op,source,target):
        identifier="component:"+op+":"+source
        components.append((identifier,op,source,target))
    for kind in ("numbers","lines","groups"):
        add("unique",kind,kind);add("sort",kind,kind);add("reverse",kind,kind)
        add("values",kind,"report");add("count",kind,"report")
    for kind in ("numbers","groups"):
        add("filter",kind,kind);add("map",kind,kind)
        for op in ("sum","mean","min","max","stats"):add(op,kind,"report")
    for op in ("lowercase","strip","words"):add(op,"lines","lines")
    add("frequencies","lines","report")
    for kind in ("numbers","lines","records"):
        add("read_"+kind,"stream","groups" if kind=="records" else kind)
    facts=[]
    for identifier,op,source,target in components:
        for predicate,obj in (("implements",op),("accepts",source),("produces",target)):
            facts.append(Claim("contract:"+digest([identifier,predicate,obj]),predicate,identifier,obj,
                               source="component-catalog-v1",origin="metadata").data())
    relations=[Relation(p).data() for p in ("implements","accepts","produces","compatible")]
    return facts,relations


def plan(program,native):
    facts,relations=catalog()
    operations=["read_"+program.input,*[s.op for s in program.steps],program.output]
    goals=[]
    for i,op in enumerate(operations):
        component="?component"+str(i)
        before="stream" if i==0 else "?type"+str(i)
        after="report" if i==len(operations)-1 else "?type"+str(i+1)
        goals.extend([Goal("implements",component,op).data(),Goal("accepts",component,before).data(),
                      Goal("produces",component,after).data()])
        if i:goals.append(Goal("compatible","?component"+str(i-1),component).data())
    result=native.query(facts,relations,[],goals,limit=2)
    if result["status"]!="complete" or not result["solutions"]:
        raise ValueError("No contract-compatible program construction exists.")
    row=result["solutions"][0]
    from upgrade.proof import ProofChecker
    claims=[Claim(**f) for f in facts]
    proof=ProofChecker(claims,relations,[]).check(row["proofs"],[Goal(**g) for g in goals],row["bindings"])
    return {"components":[row["bindings"]["?component"+str(i)] for i in range(len(operations))],
            "operations":operations,"derivation":proof,"engines":["souffle","SWI-Prolog"]}
