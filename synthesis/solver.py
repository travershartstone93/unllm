"""Use cvc5's SyGuS API for scalar holes; solutions become typed expressions."""
from __future__ import annotations
import time
import cvc5
from cvc5 import Kind
from .spec import Expr
from upgrade.model import Unsupported

KINDS={"add":Kind.ADD,"subtract":Kind.SUB,"multiply":Kind.MULT,"negate":Kind.NEG,
       "gt":Kind.GT,"ge":Kind.GEQ,"lt":Kind.LT,"le":Kind.LEQ,"eq":Kind.EQUAL,
       "and":Kind.AND,"or":Kind.OR,"not":Kind.NOT,"if":Kind.ITE}
OPS={v:k for k,v in KINDS.items()}


def synthesize_expression(target: Expr, predicate=False, timeout_ms=3000):
    start=time.monotonic()
    solver=cvc5.Solver()
    solver.setOption("sygus","true")
    solver.setOption("incremental","false")
    solver.setOption("tlimit",str(timeout_ms))
    solver.setLogic("ALL")
    real,boolean=solver.getRealSort(),solver.getBooleanSort()
    x=solver.mkVar(real,"x")
    numeric=solver.mkVar(real,"Number")
    logical=solver.mkVar(boolean,"Condition")
    constant=solver.mkVar(real,"Constant")
    first=logical if predicate else numeric
    grammar=solver.mkGrammar([x],[first,*[n for n in (numeric,logical,constant) if n!=first]])
    values=sorted({0.0,1.0,-1.0,*[float(v) for v in target.constants()]})
    grammar.addRules(constant,[solver.mkReal(str(v)) for v in values])
    # Supply the target's operation vocabulary as syntax, not as an already
    # completed program. The solver still constructs a term satisfying the
    # universally quantified specification.
    grammar.addRules(numeric,[x,constant,solver.mkTerm(Kind.ADD,numeric,numeric),
        solver.mkTerm(Kind.SUB,numeric,numeric),solver.mkTerm(Kind.MULT,constant,numeric)])
    grammar.addRules(logical,[solver.mkTerm(k,numeric,numeric) for k in
        (Kind.GT,Kind.GEQ,Kind.LT,Kind.LEQ,Kind.EQUAL)] +
        [solver.mkTerm(Kind.AND,logical,logical),solver.mkTerm(Kind.OR,logical,logical),
         solver.mkTerm(Kind.NOT,logical)])
    if "if" in str(target.data()):
        grammar.addRule(numeric,solver.mkTerm(Kind.ITE,logical,numeric,numeric))
    variable=solver.declareSygusVar("value",real)
    def encode(expr):
        if expr.op=="variable":return variable
        if expr.op=="constant":
            return solver.mkBoolean(expr.value) if isinstance(expr.value,bool) else solver.mkReal(str(expr.value))
        return solver.mkTerm(KINDS[expr.op],*[encode(a) for a in expr.args])
    function=solver.synthFun("predicate" if predicate else "transform",[x],boolean if predicate else real,grammar)
    try:
        constraint=solver.mkTerm(Kind.EQUAL,solver.mkTerm(Kind.APPLY_UF,function,variable),encode(target))
        solver.addSygusConstraint(constraint)
        result=solver.checkSynth()
    except RuntimeError as exc:
        raise Unsupported("The scalar synthesis contract could not be represented: "+str(exc)) from exc
    if not result.hasSolution():
        raise Unsupported("The scalar synthesis problem exceeded its grammar/time budget: "+str(result))
    solution=solver.getSynthSolution(function)
    def decode(term):
        kind=term.getKind()
        if kind==Kind.VARIABLE:return Expr("variable")
        if kind==Kind.CONST_RATIONAL:return Expr("constant",value=float(term.getRealValue()))
        if kind==Kind.CONST_BOOLEAN:return Expr("constant",value=term.getBooleanValue())
        if kind not in OPS:raise Unsupported("The solver returned an unsupported expression node.")
        args=tuple(decode(t) for t in term)
        op=OPS[kind]
        if len(args)>2 and op in ("add","multiply","and","or"):
            out=Expr(op,args[:2])
            for other in args[2:]:out=Expr(op,(out,other))
            return out
        return Expr(op,args)
    expression=decode(solution[1])
    return expression,{"engine":"cvc5","version":cvc5.__version__,"method":"SyGuS",
        "solution":str(solution),"elapsed_ms":round((time.monotonic()-start)*1000,3),
        "formal_domain":"real arithmetic; executable floating-point behavior checked separately"}
