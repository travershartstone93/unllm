"""Small SyGuS obligations become typed app guards and state expressions."""
import time
import cvc5
from cvc5 import Kind
from upgrade.model import Unsupported
from .ir import Value,literal,local,binary


def synthesize_rule(names,operation,timeout_ms=2000):
    start=time.monotonic();solver=cvc5.Solver()
    solver.setOption("sygus","true");solver.setOption("incremental","false");solver.setOption("tlimit",str(timeout_ms))
    solver.setLogic("LRA")
    real,boolean=solver.getRealSort(),solver.getBooleanSort()
    formals=[solver.mkVar(real,n) for n in names]
    values=[solver.declareSygusVar("input_"+n,real) for n in names]
    zero=solver.mkReal(0)
    if operation=="nonzero":
        condition=solver.mkVar(boolean,"Condition")
        grammar=solver.mkGrammar(formals,[condition])
        grammar.addRules(condition,[solver.mkBoolean(True),solver.mkBoolean(False),
            solver.mkTerm(Kind.EQUAL,formals[0],zero),solver.mkTerm(Kind.NOT,condition)])
        target=solver.mkTerm(Kind.NOT,solver.mkTerm(Kind.EQUAL,values[0],zero))
        result_type=boolean
    else:
        number=solver.mkVar(real,"Number");grammar=solver.mkGrammar(formals,[number])
        grammar.addRules(number,[*formals,zero,solver.mkTerm(Kind.ADD,number,number),solver.mkTerm(Kind.SUB,number,number)])
        target=solver.mkTerm(Kind.ADD if operation=="add" else Kind.SUB,*values);result_type=real
    function=solver.synthFun("state_rule",formals,result_type,grammar)
    solver.addSygusConstraint(solver.mkTerm(Kind.EQUAL,solver.mkTerm(Kind.APPLY_UF,function,*values),target))
    answer=solver.checkSynth()
    if not answer.hasSolution():raise Unsupported("The application scalar rule exceeded its synthesis budget.")
    solution=solver.getSynthSolution(function)
    def decode(term):
        kind=term.getKind()
        if kind==Kind.VARIABLE:
            name=str(term)
            if name not in names:raise ValueError("Unexpected scalar binding")
            return local(name)
        if kind==Kind.CONST_BOOLEAN:return literal(term.getBooleanValue())
        if kind==Kind.CONST_RATIONAL:return literal(float(term.getRealValue()))
        if kind==Kind.NOT:return Value("unary",name="not",args=(decode(term[0]),))
        ops={Kind.ADD:"add",Kind.SUB:"subtract",Kind.EQUAL:"eq"}
        if kind not in ops:raise ValueError("Unsupported synthesized application expression")
        args=[decode(a) for a in term];result=binary(ops[kind],args[0],args[1])
        for arg in args[2:]:result=binary(ops[kind],result,arg)
        return result
    expression=decode(solution[1])
    return expression,{"engine":"cvc5","version":cvc5.__version__,"method":"SyGuS","parameters":names,
        "contract":operation,"solution":str(solution),"elapsed_ms":round((time.monotonic()-start)*1000,3),
        "formal_domain":"mathematical reals; binary64 executable behavior checked separately"}
