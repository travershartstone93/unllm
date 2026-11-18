"""Synthesize simultaneous state updates from relational postconditions."""
import time
from fractions import Fraction
import cvc5
from cvc5 import Kind
from synthesis.apps.ir import Value
from upgrade.model import Unsupported
from .expressions import parse,walk


def synthesize(action,fields,timeout_ms=3000):
    started=time.monotonic()
    post,_=parse(action["ensure"],fields,transition=True)
    names=sorted(n for n,f in fields.items() if f["type"]=="number")
    outputs=sorted({v.name for v in walk(post) if v.kind=="next"})
    solver=cvc5.Solver();solver.setOption("sygus","true");solver.setOption("incremental","false")
    nonlinear=any(v.kind=="binary" and v.name=="multiply" and not any(a.kind=="literal" for a in v.args) for v in walk(post))
    solver.setOption("tlimit",str(timeout_ms));solver.setLogic("NRA" if nonlinear else "LRA")
    real,boolean=solver.getRealSort(),solver.getBooleanSort()
    formals=[solver.mkVar(real,"old_"+n) for n in names]
    inputs=[solver.declareSygusVar("input_"+n,real) for n in names]
    formal_map=dict(zip(names,formals));input_map=dict(zip(names,inputs))
    constants={Fraction(0),Fraction(1)}
    for term in walk(post):
        if term.kind=="literal" and type(term.value) in (int,float):constants.add(Fraction(str(term.value)))
    for value in tuple(constants):
        if value:constants.add(1/value)
    scalar=solver.mkVar(real,"Expression");condition=solver.mkVar(boolean,"Condition")
    grammar=solver.mkGrammar(formals,[scalar,condition])
    def number(n):return solver.mkReal(str(n.numerator)+"/"+str(n.denominator))
    grammar.addRules(scalar,[*formals,*[number(n) for n in sorted(constants)],
        solver.mkTerm(Kind.ADD,scalar,scalar),solver.mkTerm(Kind.SUB,scalar,scalar),
        solver.mkTerm(Kind.ITE,condition,scalar,scalar),
        *[solver.mkTerm(Kind.MULT,number(n),scalar) for n in sorted(constants) if n not in (0,1)]])
    grammar.addRules(condition,[solver.mkBoolean(True),solver.mkBoolean(False),
        solver.mkTerm(Kind.LEQ,scalar,scalar),solver.mkTerm(Kind.EQUAL,scalar,scalar),
        solver.mkTerm(Kind.NOT,condition),solver.mkTerm(Kind.AND,condition,condition)])
    operators={"add":Kind.ADD,"subtract":Kind.SUB,"multiply":Kind.MULT,"divide":Kind.DIVISION,
               "eq":Kind.EQUAL,"ne":Kind.DISTINCT,"lt":Kind.LT,"le":Kind.LEQ,"gt":Kind.GT,"ge":Kind.GEQ,
               "and":Kind.AND,"or":Kind.OR}
    def encode(value,previous,future):
        if value.kind=="literal":
            return solver.mkBoolean(value.value) if type(value.value) is bool else number(Fraction(str(value.value)))
        if value.kind in ("prev","state"):return previous[value.name]
        if value.kind=="next":return future[value.name]
        args=[encode(a,previous,future) for a in value.args]
        if value.kind=="conditional":return solver.mkTerm(Kind.ITE,*args)
        if value.kind=="unary":
            return args[0] if value.name=="positive" else solver.mkTerm(Kind.NOT if value.name=="not" else Kind.NEG,args[0])
        return solver.mkTerm(operators[value.name],*args)
    # Goal subterms are permitted productions, not preselected solutions. The
    # solver must satisfy the complete relation, including coupled next states.
    for term in walk(post):
        if term.kind in ("binary","conditional") and not any(t.kind=="next" for t in walk(term)):
            try:
                encoded=encode(term,formal_map,{})
                grammar.addRule(condition if encoded.getSort()==boolean else scalar,encoded)
            except (KeyError,RuntimeError):pass
    functions={name:solver.synthFun("update_"+name,formals,real,grammar) for name in outputs}
    calls={name:solver.mkTerm(Kind.APPLY_UF,fn,*inputs) if inputs else solver.mkTerm(Kind.APPLY_UF,fn)
           for name,fn in functions.items()}
    obligation=encode(post,input_map,calls);guard_encoded=False
    if "requires" in action:
        guard,_=parse(action["requires"],fields)
        scalar_guard=all(t.kind!="primitive" and (t.kind!="state" or fields[t.name]["type"]=="number") and
                         not(t.kind=="literal" and isinstance(t.value,str)) for t in walk(guard))
        if scalar_guard:
            obligation=solver.mkTerm(Kind.IMPLIES,encode(guard,input_map,{}),obligation);guard_encoded=True
    solver.addSygusConstraint(obligation)
    result=solver.checkSynth()
    if not result.hasSolution():raise Unsupported("No state-transition program was found within the synthesis budget.")
    mapping={"old_"+n:n for n in names}
    def decode(term):
        kind=term.getKind()
        if kind==Kind.VARIABLE:
            if str(term) not in mapping:raise Unsupported("Unexpected synthesized binding.")
            return Value("state",name=mapping[str(term)])
        if kind==Kind.CONST_RATIONAL:return Value("literal",value=float(term.getRealValue()))
        if kind==Kind.CONST_BOOLEAN:return Value("literal",value=term.getBooleanValue())
        args=tuple(decode(a) for a in term)
        if kind==Kind.ITE:return Value("conditional",args=args)
        if kind in (Kind.NOT,Kind.NEG):return Value("unary",name="not" if kind==Kind.NOT else "neg",args=args)
        inverse={v:k for k,v in operators.items()}
        if kind not in inverse:raise Unsupported("Unsupported synthesized expression: "+str(kind))
        value=args[0]
        for arg in args[1:]:value=Value("binary",name=inverse[kind],args=(value,arg))
        return value
    solutions={name:solver.getSynthSolution(fn) for name,fn in functions.items()}
    updates={name:decode(solution[1]) for name,solution in solutions.items()}
    return updates,{"engine":"cvc5","method":"relational SyGuS","version":cvc5.__version__,
        "postcondition":action["ensure"],"inputs":names,"outputs":outputs,
        "solutions":{n:str(t) for n,t in solutions.items()},
        "precondition":action.get("requires"),"precondition_encoded":guard_encoded,
        "simultaneous":True,"domain":"mathematical polynomial reals (degree <= 2); finite binary64 validated separately",
        "elapsed_ms":round((time.monotonic()-started)*1000,3)}
