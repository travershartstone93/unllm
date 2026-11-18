from __future__ import annotations
from dataclasses import dataclass, field, asdict
import ast
import math
import keyword
from upgrade.model import Unsupported


@dataclass(frozen=True)
class Expr:
    op: str
    args: tuple = ()
    value: float | bool | None = None

    def data(self):
        return {"op":self.op,"args":[a.data() for a in self.args],"value":self.value}

    def constants(self):
        out = [self.value] if self.op == "constant" and not isinstance(self.value,bool) else []
        return out + [v for a in self.args for v in a.constants()]

    def evaluate(self,x):
        if self.op == "variable": return x
        if self.op == "constant": return self.value
        args=[a.evaluate(x) for a in self.args]
        if self.op == "add": return args[0]+args[1]
        if self.op == "subtract": return args[0]-args[1]
        if self.op == "multiply": return args[0]*args[1]
        if self.op == "negate": return -args[0]
        if self.op == "gt": return args[0]>args[1]
        if self.op == "ge": return args[0]>=args[1]
        if self.op == "lt": return args[0]<args[1]
        if self.op == "le": return args[0]<=args[1]
        if self.op == "eq": return args[0]==args[1]
        if self.op == "and": return args[0] and args[1]
        if self.op == "or": return args[0] or args[1]
        if self.op == "not": return not args[0]
        if self.op == "if": return args[1] if args[0] else args[2]
        raise Unsupported("Unsupported expression operation: "+self.op)


def expression(value,_depth=0) -> Expr:
    if _depth>16:raise Unsupported("Expression nesting budget exceeded.")
    if isinstance(value,dict):
        if set(value)-{"op","args","value"}:raise Unsupported("Unknown expression fields.")
        args=value.get("args",[])
        if not isinstance(args,list) or len(args)>3:raise Unsupported("An expression node has too many arguments.")
        result=Expr(value["op"],tuple(expression(a,_depth+1) for a in args),value.get("value"))
        expression_type(result)
        return result
    if not isinstance(value,str) or len(value)>1000:
        raise Unsupported("An expression must be a small typed tree or Python expression.")
    def convert(node,depth=0):
        if depth>16: raise Unsupported("Expression nesting budget exceeded.")
        if isinstance(node,ast.Name) and node.id=="x":return Expr("variable")
        if isinstance(node,ast.Constant) and isinstance(node.value,(int,float,bool)):
            if not math.isfinite(float(node.value)) or abs(float(node.value))>1e12:
                raise Unsupported("Expression constants must be finite and bounded.")
            return Expr("constant",value=node.value)
        if isinstance(node,ast.BinOp) and type(node.op) in (ast.Add,ast.Sub,ast.Mult):
            op={ast.Add:"add",ast.Sub:"subtract",ast.Mult:"multiply"}[type(node.op)]
            return Expr(op,(convert(node.left,depth+1),convert(node.right,depth+1)))
        if isinstance(node,ast.UnaryOp) and isinstance(node.op,(ast.USub,ast.Not)):
            return Expr("negate" if isinstance(node.op,ast.USub) else "not",(convert(node.operand,depth+1),))
        if isinstance(node,ast.Compare) and len(node.ops)==1 and type(node.ops[0]) in (
                ast.Gt,ast.GtE,ast.Lt,ast.LtE,ast.Eq):
            op={ast.Gt:"gt",ast.GtE:"ge",ast.Lt:"lt",ast.LtE:"le",ast.Eq:"eq"}[type(node.ops[0])]
            return Expr(op,(convert(node.left,depth+1),convert(node.comparators[0],depth+1)))
        if isinstance(node,ast.BoolOp) and isinstance(node.op,(ast.And,ast.Or)):
            values=[convert(v,depth+1) for v in node.values]
            result=values[0]
            for other in values[1:]:result=Expr("and" if isinstance(node.op,ast.And) else "or",(result,other))
            return result
        if isinstance(node,ast.IfExp):
            return Expr("if",(convert(node.test,depth+1),convert(node.body,depth+1),convert(node.orelse,depth+1)))
        raise Unsupported("Only x, bounded constants, arithmetic, comparisons and conditionals are supported.")
    result=convert(ast.parse(value,mode="eval").body)
    expression_type(result)
    return result


def expression_type(expr):
    arities={"variable":0,"constant":0,"negate":1,"not":1,"add":2,"subtract":2,"multiply":2,
             "gt":2,"ge":2,"lt":2,"le":2,"eq":2,"and":2,"or":2,"if":3}
    if expr.op not in arities or len(expr.args)!=arities[expr.op]:
        raise Unsupported("Invalid expression operation or arity.")
    if expr.op=="constant":
        if type(expr.value) not in (int,float,bool):raise Unsupported("Constants must be numeric or boolean.")
        if not math.isfinite(float(expr.value)) or abs(float(expr.value))>1e12:
            raise Unsupported("Expression constants must be finite and bounded.")
        return "bool" if type(expr.value) is bool else "number"
    if expr.value is not None:raise Unsupported("Only constants have a literal value.")
    if expr.op=="variable":return "number"
    types=[expression_type(a) for a in expr.args]
    if expr.op in ("and","or","not"):
        if any(t!="bool" for t in types):raise Unsupported("Logical operations need boolean operands.")
        return "bool"
    if expr.op=="if":
        if types[0]!="bool" or types[1]!=types[2]:raise Unsupported("A conditional needs a boolean and matching branch types.")
        return types[1]
    if any(t!="number" for t in types):raise Unsupported("Arithmetic and comparison operands must be numeric.")
    return "bool" if expr.op in ("gt","ge","lt","le","eq") else "number"


@dataclass
class Step:
    op: str
    expr: Expr | None = None
    descending: bool = False

    def data(self):
        return {"op":self.op,"expr":self.expr.data() if self.expr else None,"descending":self.descending}


@dataclass
class Program:
    input: str
    steps: list[Step]
    output: str
    invalid: str = "error"
    name: str = "process_data"
    assumptions: list[str] = field(default_factory=list)
    schema: int = 1

    @classmethod
    def from_dict(cls,data):
        if data.get("schema",1)!=1:raise Unsupported("Unsupported program specification schema.")
        unknown=set(data)-{"schema","input","steps","output","invalid","name","assumptions"}
        if unknown:raise Unsupported("Unknown specification fields: "+", ".join(sorted(unknown)))
        kind=data.get("input")
        if isinstance(kind,dict):
            if set(kind)-{"kind"}:raise Unsupported("Unsupported input schema fields.")
            kind=kind.get("kind")
        if kind not in ("numbers","lines","records"):raise Unsupported("Input must be numbers, lines or records.")
        output=data.get("output","values")
        if output not in ("values","sum","mean","min","max","count","stats","frequencies"):
            raise Unsupported("Unsupported output contract.")
        invalid=data.get("invalid","error")
        if invalid not in ("error","skip"):raise Unsupported("Invalid-record handling must be error or skip.")
        steps=[]
        for item in data.get("steps",[]):
            if not isinstance(item,dict):raise Unsupported("Each operation needs an object.")
            if set(item)-{"op","expr","descending"}:raise Unsupported("Unknown operation fields.")
            op=item["op"]
            if op not in ("filter","map","unique","sort","reverse","lowercase","strip","words"):
                raise Unsupported("Unsupported operation: "+op)
            expr=expression(item["expr"]) if item.get("expr") is not None else None
            if op in ("filter","map") and expr is None:raise Unsupported(op+" requires an expression.")
            if op not in ("filter","map") and expr is not None:raise Unsupported("Only filter and map take expressions.")
            if expr and expression_type(expr)!=("bool" if op=="filter" else "number"):
                raise Unsupported("Filters need boolean expressions; maps need numeric expressions.")
            if type(item.get("descending",False)) is not bool:raise Unsupported("Sort direction must be boolean.")
            if item.get("descending") and op!="sort":raise Unsupported("Only sorting has a direction.")
            if op in ("filter","map") and kind=="lines":raise Unsupported("Numeric expressions need numeric input.")
            if op in ("lowercase","strip","words") and kind!="lines":raise Unsupported("Text operations need text input.")
            steps.append(Step(op,expr,bool(item.get("descending",False))))
        if kind=="lines" and output not in ("values","count","frequencies"):
            raise Unsupported("Text output supports values, count and frequencies.")
        if kind!="lines" and output=="frequencies":
            raise Unsupported("Frequency output currently needs text input.")
        if len(steps)>12:raise Unsupported("The program operation budget is 12.")
        name=data.get("name","process_data")
        if not isinstance(name,str) or not name.isidentifier() or name.startswith("_") or keyword.iskeyword(name):
            raise Unsupported("The function name must be a public identifier.")
        return cls(kind,steps,output,invalid,name,list(data.get("assumptions",[])))

    def data(self):
        return {"schema":self.schema,"input":self.input,"steps":[s.data() for s in self.steps],
                "output":self.output,"invalid":self.invalid,"name":self.name,"assumptions":self.assumptions}
