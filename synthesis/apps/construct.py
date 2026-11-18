"""Build controller functions, state and UI from a checked component plan."""
from dataclasses import replace
from .ir import *
from .solver import synthesize_rule


def substitute(value,mapping):
    if value.kind=="local" and value.name in mapping:return mapping[value.name]
    return replace(value,args=tuple(substitute(a,mapping) for a in value.args))


def construct(spec,plan):
    providers=plan["providers"]
    required=set(plan["requirements"])
    wanted={"app:interactive_expression",*("arithmetic:"+op for op in spec.operations),
            *("control:"+name for name in ("memory","parentheses","backspace","keyboard") if getattr(spec,name))}
    if required!=wanted or not wanted.issubset(providers):raise ValueError("The application plan does not match its requirements")
    reports=[]
    fields={"entry":(TEXT,""),"after_result":(BOOL,False),"error":(TEXT,""),"last_value":(NUMBER,0.0)}
    if spec.memory:fields["memory"]=(NUMBER,0.0)
    functions=[]
    def fn(name,params,result,body):functions.append(Function(name,tuple(params),result,tuple(body)))
    E=state("entry");R=state("after_result");ERROR=state("error")
    empty=binary("eq",E,literal(""))
    fn("clear",[],VOID,[set_state("entry",literal("")),set_state("error",literal("")),set_state("after_result",literal(False))])
    fn("write_result",[("value",NUMBER)],VOID,[
        set_state("last_value",local("value")),set_state("entry",primitive("number_text",local("value"))),
        set_state("after_result",literal(True)),set_state("error",literal(""))])
    fn("current_value",[],NUMBER,[branch(empty,[ret(literal(0.0))]),ret(primitive("evaluate_expression",E))])
    fn("append",[("text",TEXT),("fresh",BOOL)],VOID,[
        branch(binary("ne",ERROR,literal("")),[call("clear")]),
        branch(binary("and",R,local("fresh")),[call("clear")]),
        branch(binary("gt",binary("add",primitive("length",E),primitive("length",local("text"))),literal(1024.0)),
               [set_state("error",literal("Expression too long")),ret()]),
        set_state("after_result",literal(False)),set_state("entry",binary("add",E,local("text")))])
    if spec.backspace:
        fn("backspace",[],VOID,[
            branch(binary("or",R,binary("ne",ERROR,literal(""))),[call("clear"),ret()]),
            set_state("entry",primitive("truncate_last",E))])
    catch=[set_state("error",local("problem")),set_state("after_result",literal(False))]
    fn("evaluate",[],VOID,[attempt([call("write_result",invoke("current_value"))],catch)])
    fn("render",[],TEXT,[
        branch(binary("ne",ERROR,literal("")),[ret(binary("add",literal("Error: "),ERROR))]),
        branch(R,[ret(primitive("display_number",state("last_value"),literal(spec.precision)))]),
        branch(empty,[ret(literal("0"))]),ret(E)])
    if spec.memory:
        for op in ("add","subtract"):
            rule,report=synthesize_rule(["memory_value","operand"],op);reports.append(report)
            expression=substitute(rule,{"memory_value":state("memory"),"operand":local("operand")})
            fn("memory_"+op,[],VOID,[attempt([
                let("operand",invoke("current_value")),set_state("memory",primitive("checked_number",expression)),
                set_state("error",literal(""))],catch)])
        fn("memory_clear",[],VOID,[set_state("memory",literal(0.0))])
        fn("memory_recall",[],VOID,[call("write_result",state("memory"))])
        fn("memory_display",[],TEXT,[ret(binary("add",literal("Memory: "),primitive("display_number",state("memory"),literal(spec.precision))))])
    operators=[]
    operation_symbols={"add":"+","subtract":"-","multiply":"*","divide":"/"}
    body=[]
    guard=None
    if "divide" in spec.operations:
        guard,report=synthesize_rule(["divisor"],"nonzero");reports.append(report)
    for op in spec.operations:
        expression=binary(op,local("left"),local("right"))
        action=[ret(primitive("checked_number",expression))]
        if op=="divide":
            valid=substitute(guard,{"divisor":local("right")})
            action.insert(0,branch(Value("unary",name="not",args=(valid,)),[fail("Division by zero")]))
        body.append(branch(binary("eq",local("operation"),literal(operation_symbols[op])),action))
    body.append(fail("Operation is not enabled"))
    operators.append(Function("apply_operator",(("operation",TEXT),("left",NUMBER),("right",NUMBER)),NUMBER,tuple(body)))
    widgets=[Widget("display","display",0,0,binding=Binding("render"),span=4)]
    keys={};row=1
    def button(identifier,label,r,c,function,*args):
        widgets.append(Widget(identifier,"button",r,c,label,Binding(function,tuple(args))))
    if spec.memory:
        widgets.append(Widget("memory_display","label",row,0,binding=Binding("memory_display"),span=4));row+=1
        for col,(identifier,label) in enumerate((("memory_clear","MC"),("memory_recall","MR"),("memory_add","M+"),("memory_subtract","M-"))):
            button(identifier,label,row,col,identifier)
        row+=1
    button("clear","C",row,0,"clear")
    if spec.backspace:button("backspace","⌫",row,1,"backspace")
    if spec.parentheses:
        button("lparen","(",row,2,"append","(",True);button("rparen",")",row,3,"append",")",False)
    row+=1
    for offset,digits in enumerate(("789","456","123")):
        for col,digit in enumerate(digits):button("digit_"+digit,digit,row+offset,col,"append",digit,True)
    button("digit_0","0",row+3,0,"append","0",True)
    button("decimal",".",row+3,1,"append",".",True)
    button("evaluate","=",row+3,2,"evaluate")
    for op,(label,offset) in {"divide":("÷",0),"multiply":("×",1),"subtract":("−",2),"add":("+",3)}.items():
        if op in spec.operations:button(op,label,row+offset,3,"append",operation_symbols[op],False)
    if spec.keyboard:
        for digit in "0123456789":keys[digit]=Binding("append",(digit,True))
        keys["."]=Binding("append",(".",True))
        # The evaluator enforces the enabled operator set. Retaining keyboard
        # tokens gives an explicit error rather than silently joining digits
        # when a disabled operator is typed.
        for symbol in operation_symbols.values():keys[symbol]=Binding("append",(symbol,False))
        # Unary signs are part of number input even when a binary operation is disabled.
        if "subtract" not in spec.operations:keys["-"]=Binding("append",("-",False))
        if spec.parentheses:
            keys["("]=Binding("append",("(",True));keys[")"]=Binding("append",(")",False))
        keys["Return"]=keys["Enter"]=keys["="]=Binding("evaluate")
        keys["Escape"]=keys["Delete"]=keys["c"]=keys["C"]=Binding("clear")
        if spec.backspace:keys["Backspace"]=Binding("backspace")
    app=Application(fields,functions,widgets,keys,operators,spec.title,spec.precision,
                    plan["requirements"],plan["components"]).validate()
    return app,reports
