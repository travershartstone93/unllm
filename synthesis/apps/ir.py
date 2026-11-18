"""Language-independent values, control flow, state, widgets and bindings."""
from dataclasses import dataclass,field,asdict
import keyword
import math
from upgrade.model import Unsupported

NUMBER="number"
TEXT="text"
BOOL="bool"
VOID="void"

PRIMITIVES={
    "evaluate_expression":((TEXT,),NUMBER),
    "checked_number":((NUMBER,),NUMBER),
    "number_text":((NUMBER,),TEXT),
    "display_number":((NUMBER,NUMBER),TEXT),
    "length":((TEXT,),NUMBER),
    "truncate_last":((TEXT,),TEXT),
}


@dataclass(frozen=True)
class Value:
    kind: str
    name: str=""
    value: str|float|bool|None=None
    args: tuple=()

    def data(self):return asdict(self)


def literal(value):return Value("literal",value=value)
def state(name):return Value("state",name=name)
def local(name):return Value("local",name=name)
def binary(op,a,b):return Value("binary",name=op,args=(a,b))
def invoke(name,*args):return Value("invoke",name=name,args=tuple(args))
def primitive(name,*args):return Value("primitive",name=name,args=tuple(args))


@dataclass(frozen=True)
class Statement:
    kind: str
    name: str=""
    value: Value|None=None
    body: tuple=()
    otherwise: tuple=()


def set_state(name,value):return Statement("set",name,value)
def let(name,value):return Statement("let",name,value)
def ret(value=None):return Statement("return",value=value)
def call(name,*args):return Statement("call",value=invoke(name,*args))
def branch(condition,body,otherwise=()):return Statement("if",value=condition,body=tuple(body),otherwise=tuple(otherwise))
def attempt(body,catch):return Statement("try",name="problem",body=tuple(body),otherwise=tuple(catch))
def fail(message):return Statement("raise",value=literal(message))


@dataclass(frozen=True)
class Function:
    name: str
    parameters: tuple
    result: str
    body: tuple


@dataclass(frozen=True)
class Binding:
    function: str
    arguments: tuple=()


@dataclass(frozen=True)
class Widget:
    identifier: str
    kind: str
    row: int
    column: int
    label: str=""
    binding: Binding|None=None
    span: int=1


@dataclass
class Application:
    state: dict
    functions: list[Function]
    widgets: list[Widget]
    keys: dict[str,Binding]
    operators: list[Function]
    title: str
    precision: int
    requirements: list[str]
    components: list[str]

    def data(self):return asdict(self)

    def validate(self):
        def identifier(name):
            if not name.isidentifier() or keyword.iskeyword(name) or name.startswith("_"):
                raise Unsupported("Invalid application identifier.")
        for name in self.state:identifier(name)
        fields={name:kind for name,(kind,_) in self.state.items()}
        methods={f.name:(tuple(t for _,t in f.parameters),f.result) for f in self.functions}
        if len(methods)!=len(self.functions):raise Unsupported("Repeated application function.")
        def check_value(value,scope):
            if value.kind=="literal":
                if type(value.value) is bool:return BOOL
                if isinstance(value.value,str):return TEXT
                if type(value.value) in (int,float):
                    if not math.isfinite(value.value) or abs(value.value)>1e15:raise Unsupported("Unbounded IR literal.")
                    return NUMBER
                raise Unsupported("Invalid IR literal.")
            if value.kind in ("state","local"):
                environment=fields if value.kind=="state" else scope
                if value.name not in environment:raise Unsupported("Unbound IR value: "+value.name)
                return environment[value.name]
            types=tuple(check_value(a,scope) for a in value.args)
            if value.kind=="unary" and value.name=="not" and types==(BOOL,):return BOOL
            if value.kind in ("invoke","primitive"):
                functions=methods if value.kind=="invoke" else PRIMITIVES
                if value.name not in functions or types!=functions[value.name][0]:
                    raise Unsupported("Invalid application call: "+value.name)
                return functions[value.name][1]
            if value.kind=="binary":
                if value.name in ("eq","ne"):
                    if len(types)!=2 or types[0]!=types[1]:raise Unsupported("Comparison type mismatch.")
                    return BOOL
                if value.name in ("and","or"):
                    if types!=(BOOL,BOOL):raise Unsupported("Boolean operands required.")
                    return BOOL
                if value.name in ("gt","ge","lt","le"):
                    if types!=(NUMBER,NUMBER):raise Unsupported("Numeric operands required.")
                    return BOOL
                if value.name=="add" and types==(TEXT,TEXT):return TEXT
                if value.name not in ("add","subtract","multiply","divide") or types!=(NUMBER,NUMBER):
                    raise Unsupported("Invalid arithmetic IR expression.")
                return NUMBER
            raise Unsupported("Unsupported IR expression.")
        def check_body(body,scope,result):
            scope=dict(scope)
            for statement in body:
                value_type=check_value(statement.value,scope) if statement.value is not None else VOID
                if statement.kind=="set":
                    if fields.get(statement.name)!=value_type:raise Unsupported("State assignment type mismatch.")
                elif statement.kind=="let":
                    identifier(statement.name)
                    if statement.name in scope:raise Unsupported("Repeated local declaration.")
                    scope[statement.name]=value_type
                elif statement.kind=="return":
                    if value_type!=result:raise Unsupported("Function return type mismatch.")
                elif statement.kind=="call":
                    if value_type!=VOID:raise Unsupported("Discarded application value.")
                elif statement.kind=="if":
                    if value_type!=BOOL:raise Unsupported("Boolean branch condition required.")
                    check_body(statement.body,scope,result);check_body(statement.otherwise,scope,result)
                elif statement.kind=="try":
                    check_body(statement.body,scope,result)
                    check_body(statement.otherwise,{**scope,statement.name:TEXT},result)
                elif statement.kind=="raise":
                    if value_type!=TEXT:raise Unsupported("An error needs a message.")
                else:raise Unsupported("Unsupported IR statement.")
        for name,(kind,value) in self.state.items():
            if check_value(literal(value),{})!=kind:raise Unsupported("State initializer type mismatch.")
        for function in self.functions:
            identifier(function.name)
            check_body(function.body,dict(function.parameters),function.result)
        for function in self.operators:
            check_body(function.body,dict(function.parameters),function.result)
        ids=set()
        def check_binding(binding,expected=VOID):
            if binding.function not in methods:raise Unsupported("Unbound event handler.")
            values=tuple(check_value(literal(a),{}) for a in binding.arguments)
            if methods[binding.function]!=(values,expected):raise Unsupported("Event binding type mismatch.")
        for widget in self.widgets:
            if widget.identifier in ids:raise Unsupported("Duplicate widget identity.")
            ids.add(widget.identifier)
            if widget.kind=="button":check_binding(widget.binding)
            elif widget.kind in ("display","label"):check_binding(widget.binding,TEXT)
            else:raise Unsupported("Unsupported widget kind.")
        for binding in self.keys.values():check_binding(binding)
        return self
