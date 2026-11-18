"""Typed expression trees from Python's parser, without evaluating source."""
import ast
import math
from upgrade.model import Unsupported
from synthesis.apps.ir import Value

NUMBER,TEXT,BOOL,RECORDS="number","text","bool","records"
CALLS={
    "upper":((TEXT,),TEXT),"lower":((TEXT,),TEXT),"strip":((TEXT,),TEXT),
    "reverse_lines":((TEXT,),TEXT),"sort_lines":((TEXT,),TEXT),"unique_lines":((TEXT,),TEXT),
    "words":((TEXT,),NUMBER),"chars":((TEXT,),NUMBER),"lines":((TEXT,),NUMBER),
    "number_text":((NUMBER,),TEXT),"replace":((TEXT,TEXT,TEXT),TEXT),
    "records_count":((RECORDS,),NUMBER),"count_true":((RECORDS,TEXT),NUMBER),
}
OPS={ast.Add:"add",ast.Sub:"subtract",ast.Mult:"multiply",ast.Div:"divide",
     ast.Eq:"eq",ast.NotEq:"ne",ast.Lt:"lt",ast.LtE:"le",ast.Gt:"gt",ast.GtE:"ge"}


def parse(source,fields,*,transition=False):
    if not isinstance(source,str) or len(source)>2048:raise Unsupported("Expression budget exceeded.")
    try:tree=ast.parse(source,mode="eval").body
    except (SyntaxError,ValueError) as error:raise Unsupported("Invalid requirement expression.") from error
    if sum(1 for _ in ast.walk(tree))>128:raise Unsupported("Expression tree budget exceeded.")
    def visit(node):
        if isinstance(node,ast.Constant):
            value=node.value
            kind=BOOL if type(value) is bool else TEXT if isinstance(value,str) else NUMBER
            if kind==NUMBER and (type(value) not in (int,float) or not math.isfinite(value) or abs(value)>1e15):
                raise Unsupported("Invalid expression literal.")
            return Value("literal",value=value),kind
        if isinstance(node,ast.Name) and not transition:
            if node.id not in fields:raise Unsupported("Unbound state: "+node.id)
            return Value("state",name=node.id),fields[node.id]["type"]
        if isinstance(node,ast.List) and not node.elts:return Value("records",value=[]),RECORDS
        if isinstance(node,ast.Attribute) and transition and isinstance(node.value,ast.Name) and node.value.id in ("prev","next"):
            if node.attr not in fields:raise Unsupported("Unbound transition state: "+node.attr)
            return Value(node.value.id,name=node.attr),fields[node.attr]["type"]
        if isinstance(node,ast.Call) and isinstance(node.func,ast.Name) and node.func.id in CALLS and not node.keywords:
            args=[visit(a) for a in node.args];signature,result=CALLS[node.func.id]
            if tuple(t for _,t in args)!=signature:raise Unsupported("Primitive argument types do not match: "+node.func.id)
            if node.func.id=="count_true":
                source,args_field=args[0][0],args[1][0]
                if source.kind!="state" or args_field.kind!="literal" or fields[source.name].get("fields",{}).get(args_field.value)!=BOOL:
                    raise Unsupported("count_true needs a declared Boolean record field.")
            return Value("primitive",name=node.func.id,args=tuple(v for v,_ in args)),result
        if isinstance(node,ast.UnaryOp) and isinstance(node.op,(ast.Not,ast.USub,ast.UAdd)):
            value,kind=visit(node.operand)
            wanted=BOOL if isinstance(node.op,ast.Not) else NUMBER
            if kind!=wanted:raise Unsupported("Unary operand type mismatch.")
            return Value("unary",name="not" if wanted==BOOL else "neg" if isinstance(node.op,ast.USub) else "positive",args=(value,)),kind
        if isinstance(node,ast.BoolOp):
            args=[visit(a) for a in node.values]
            if any(t!=BOOL for _,t in args):raise Unsupported("Boolean operands required.")
            result=args[0][0]
            for value,_ in args[1:]:result=Value("binary",name="and" if isinstance(node.op,ast.And) else "or",args=(result,value))
            return result,BOOL
        if isinstance(node,ast.IfExp):
            condition,ct=visit(node.test);yes,yt=visit(node.body);no,nt=visit(node.orelse)
            if ct!=BOOL or yt!=nt:raise Unsupported("Conditional expression types differ.")
            return Value("conditional",args=(condition,yes,no)),yt
        if isinstance(node,ast.Compare):
            previous,pt=visit(node.left);result=None
            for operator,operand in zip(node.ops,node.comparators):
                current,ct=visit(operand);op=OPS.get(type(operator))
                if op is None or pt!=ct or (op not in ("eq","ne") and pt!=NUMBER):raise Unsupported("Comparison type mismatch.")
                clause=Value("binary",name=op,args=(previous,current))
                result=clause if result is None else Value("binary",name="and",args=(result,clause))
                previous,pt=current,ct
            return result,BOOL
        if isinstance(node,ast.BinOp) and type(node.op) in OPS:
            left,lt=visit(node.left);right,rt=visit(node.right);op=OPS[type(node.op)]
            if not(lt==rt==NUMBER or op=="add" and lt==rt==TEXT):raise Unsupported("Arithmetic type mismatch.")
            return Value("binary",name=op,args=(left,right)),lt
        raise Unsupported("Unsupported expression construct: "+type(node).__name__)
    return visit(tree)


def walk(value):
    yield value
    for arg in value.args:yield from walk(arg)


def evaluate(value,state):
    """Independent specification interpreter; emitted programs do not use it."""
    if value.kind=="literal":return value.value
    if value.kind=="records":return []
    if value.kind=="state":return state[value.name]
    if value.kind in ("prev","next"):return state[value.kind][value.name]
    if value.kind=="conditional":
        condition=evaluate(value.args[0],state)
        return evaluate(value.args[1] if condition else value.args[2],state)
    if value.kind=="binary" and value.name in ("and","or"):
        left=evaluate(value.args[0],state)
        return (left and evaluate(value.args[1],state)) if value.name=="and" else (left or evaluate(value.args[1],state))
    args=[evaluate(a,state) for a in value.args]
    if value.kind=="unary":return not args[0] if value.name=="not" else -args[0] if value.name=="neg" else args[0]
    if value.kind=="primitive":
        s=args[0]
        return {"upper":lambda:s.upper(),"lower":lambda:s.lower(),"strip":lambda:s.strip(),
                "reverse_lines":lambda:"\n".join(reversed(s.splitlines())),
                "sort_lines":lambda:"\n".join(sorted(s.splitlines())),
                "unique_lines":lambda:"\n".join(dict.fromkeys(s.splitlines())),
                "words":lambda:float(len(s.split())),"chars":lambda:float(len(s)),
                "lines":lambda:float(len(s.splitlines())),"number_text":lambda:format(s,".12g"),
                "replace":lambda:s.replace(args[1],args[2]),
                "records_count":lambda:float(len(s)),
                "count_true":lambda:float(sum(row[args[1]] for row in s))}[value.name]()
    a,b=args
    result={"add":lambda:a+b,"subtract":lambda:a-b,"multiply":lambda:a*b,"divide":lambda:a/b,
            "eq":lambda:a==b,"ne":lambda:a!=b,"gt":lambda:a>b,"ge":lambda:a>=b,
            "lt":lambda:a<b,"le":lambda:a<=b,"and":lambda:a and b,"or":lambda:a or b}[value.name]()
    if type(result) in (int,float) and (not math.isfinite(result) or abs(result)>1e15):raise ValueError("Numeric domain")
    return result
