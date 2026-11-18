"""Structural lowering of typed application functions and widget bindings."""
from pathlib import Path
import ast
import json
from .ir import NUMBER,TEXT,BOOL,VOID

COMPONENTS=Path(__file__).parent/"components"
CPP_TYPES={NUMBER:"double",TEXT:"std::string",BOOL:"bool",VOID:"void"}


def py_value(value):
    if value.kind=="literal":return ast.Constant(value.value)
    if value.kind=="state":return ast.Attribute(ast.Name("self",ast.Load()),value.name,ast.Load())
    if value.kind=="local":return ast.Name(value.name,ast.Load())
    args=[py_value(a) for a in value.args]
    if value.kind in ("invoke","primitive"):
        name=ast.Attribute(ast.Name("self",ast.Load()),value.name,ast.Load()) if value.kind=="invoke" else ast.Name(value.name,ast.Load())
        return ast.Call(name,args,[])
    if value.kind=="unary":return ast.UnaryOp(ast.Not(),args[0])
    if value.name in ("add","subtract","multiply","divide"):
        return ast.BinOp(args[0],{"add":ast.Add(),"subtract":ast.Sub(),"multiply":ast.Mult(),"divide":ast.Div()}[value.name],args[1])
    if value.name in ("and","or"):return ast.BoolOp(ast.And() if value.name=="and" else ast.Or(),args)
    return ast.Compare(args[0],[{"eq":ast.Eq(),"ne":ast.NotEq(),"gt":ast.Gt(),"ge":ast.GtE(),"lt":ast.Lt(),"le":ast.LtE()}[value.name]],[args[1]])


def py_body(statements):
    body=[]
    for s in statements:
        value=py_value(s.value) if s.value else None
        if s.kind=="set":body.append(ast.Assign([ast.Attribute(ast.Name("self",ast.Load()),s.name,ast.Store())],value))
        elif s.kind=="let":body.append(ast.Assign([ast.Name(s.name,ast.Store())],value))
        elif s.kind=="return":body.append(ast.Return(value))
        elif s.kind=="call":body.append(ast.Expr(value))
        elif s.kind=="raise":body.append(ast.Raise(ast.Call(ast.Name("ValueError",ast.Load()),[value],[])))
        elif s.kind=="if":body.append(ast.If(value,py_body(s.body),py_body(s.otherwise)))
        elif s.kind=="try":
            catch=[ast.Assign([ast.Name(s.name,ast.Store())],ast.Call(ast.Name("str",ast.Load()),[ast.Name("caught",ast.Load())],[])),*py_body(s.otherwise)]
            body.append(ast.Try(py_body(s.body),[ast.ExceptHandler(ast.Name("ValueError",ast.Load()),"caught",catch)],[],[]))
    return body


def py_function(function,method):
    args=([ast.arg("self")] if method else [])+[ast.arg(name) for name,_ in function.parameters]
    return ast.FunctionDef(function.name,ast.arguments(posonlyargs=[],args=args,kwonlyargs=[],kw_defaults=[],defaults=[]),
                           py_body(function.body),[])


def py_call(name,*arguments):
    return ast.Expr(ast.Call(ast.Attribute(ast.Name("self",ast.Load()),name,ast.Load()),list(arguments),[]))


def py_binding(binding):
    target=ast.Attribute(ast.Attribute(ast.Name("self",ast.Load()),"controller",ast.Load()),binding.function,ast.Load())
    return ast.Lambda(ast.arguments(posonlyargs=[],args=[],kwonlyargs=[],kw_defaults=[],defaults=[]),
                      ast.Call(target,[ast.Constant(a) for a in binding.arguments],[]))


def python(app,spec):
    app.validate()
    module=ast.parse((COMPONENTS/"python_expression.py").read_text())
    module.body.insert(0,ast.Assign([ast.Name("ALLOW_PARENTHESES",ast.Store())],ast.Constant(spec.parentheses)))
    module.body.extend(py_function(f,False) for f in app.operators)
    initial=[ast.Assign([ast.Attribute(ast.Name("self",ast.Load()),name,ast.Store())],ast.Constant(value))
             for name,(_,value) in app.state.items()]
    controller=ast.ClassDef("Controller",[],[],[
        ast.FunctionDef("__init__",ast.arguments(posonlyargs=[],args=[ast.arg("self")],kwonlyargs=[],kw_defaults=[],defaults=[]),initial,[]),
        *[py_function(f,True) for f in app.functions]],[])
    module.body.append(controller)
    module.body.extend(ast.parse((COMPONENTS/"python_widgets.py").read_text()).body)
    init=ast.parse("super().__init__()\nself.controller = Controller()\n").body
    init.append(py_call("setWindowTitle",ast.Constant(app.title)))
    for w in app.widgets:
        if w.kind=="button":
            init.append(py_call("add_button",*[ast.Constant(a) for a in (w.identifier,w.label,w.row,w.column)],py_binding(w.binding)))
        else:
            init.append(py_call("add_display",*[ast.Constant(a) for a in (w.identifier,w.kind,w.row,w.column,w.span)],py_binding(w.binding)))
    for key,binding in app.keys.items():init.append(py_call("bind_key",ast.Constant(key),py_binding(binding)))
    init.append(py_call("refresh"))
    window=ast.ClassDef("ApplicationWindow",[ast.Name("GenericWindow",ast.Load())],[],[
        ast.FunctionDef("__init__",ast.arguments(posonlyargs=[],args=[ast.arg("self")],kwonlyargs=[],kw_defaults=[],defaults=[]),init,[])],[])
    module.body.append(window)
    module.body.extend(ast.parse('''def main():
    application=QApplication(sys.argv)
    application.setStyle("Fusion")
    window=ApplicationWindow()
    window.show()
    return application.exec()

if __name__=="__main__":
    raise SystemExit(main())
''').body)
    ast.fix_missing_locations(module);compile(module,"<application>","exec")
    return ast.unparse(module)+"\n"


def cpp_literal(value):
    if type(value) is bool:return "true" if value else "false"
    if isinstance(value,str):return json.dumps(value,ensure_ascii=False)
    return repr(float(value))


def cpp_value(value):
    if value.kind=="literal":return cpp_literal(value.value)
    if value.kind in ("state","local"):return value.name
    args=[cpp_value(a) for a in value.args]
    if value.kind in ("invoke","primitive"):return value.name+"("+", ".join(args)+")"
    if value.kind=="unary":return "(!"+args[0]+")"
    op={"add":"+","subtract":"-","multiply":"*","divide":"/","eq":"==","ne":"!=","gt":">","ge":">=","lt":"<","le":"<=","and":"&&","or":"||"}[value.name]
    return "("+args[0]+" "+op+" "+args[1]+")"


def cpp_body(statements):
    lines=[]
    for s in statements:
        v=cpp_value(s.value) if s.value else ""
        if s.kind=="set":lines.append(s.name+" = "+v+";")
        elif s.kind=="let":lines.append("const auto "+s.name+" = "+v+";")
        elif s.kind=="return":lines.append("return"+(" "+v if v else "")+";")
        elif s.kind=="call":lines.append(v+";")
        elif s.kind=="raise":lines.append("throw std::runtime_error("+v+");")
        elif s.kind=="if":
            lines+=["if ("+v+") {",*cpp_body(s.body),"}"]
            if s.otherwise:lines+=["else {",*cpp_body(s.otherwise),"}"]
        elif s.kind=="try":
            lines+=["try {",*cpp_body(s.body),"} catch (const std::exception& caught) {",
                    "const std::string "+s.name+" = caught.what();",*cpp_body(s.otherwise),"}"]
    return lines


def cpp_function(f):
    return [CPP_TYPES[f.result]+" "+f.name+"("+", ".join(CPP_TYPES[t]+" "+name for name,t in f.parameters)+") {",
            *cpp_body(f.body),"}"]


def cpp_binding(binding,result=False):
    call="controller."+binding.function+"("+", ".join(cpp_literal(a) for a in binding.arguments)+");"
    return "[this] { "+("return " if result else "")+call+" }"


def cpp(app,spec):
    app.validate()
    lines=["constexpr bool ALLOW_PARENTHESES = "+cpp_literal(spec.parentheses)+";",
           (COMPONENTS/"cpp_expression.h").read_text()]
    for function in app.operators:lines+=cpp_function(function)
    lines+=["struct Controller {"]
    for name,(kind,value) in app.state.items():lines.append(CPP_TYPES[kind]+" "+name+" = "+cpp_literal(value)+";")
    for function in app.functions:lines+=cpp_function(function)
    lines+=["};",(COMPONENTS/"cpp_widgets.h").read_text(),"class ApplicationWindow:public GenericWindow {",
            "public:","Controller controller;","ApplicationWindow() {","setWindowTitle(QString::fromUtf8("+cpp_literal(app.title)+"));"]
    for w in app.widgets:
        if w.kind=="button":
            args=[cpp_literal(w.identifier),cpp_literal(w.label),str(w.row),str(w.column),cpp_binding(w.binding)]
            lines.append("add_button("+", ".join(args)+");")
        else:
            args=[cpp_literal(w.identifier),cpp_literal(w.kind),str(w.row),str(w.column),str(w.span),cpp_binding(w.binding,True)]
            lines.append("add_display("+", ".join(args)+");")
    for key,binding in app.keys.items():lines.append("bind_key("+cpp_literal(key)+", "+cpp_binding(binding)+");")
    lines+=["refresh();","}","};","#ifndef UNLLM_APP_LIBRARY","int main(int argc,char** argv) {",
            "QApplication application(argc,argv);application.setStyle(\"Fusion\");",
            "ApplicationWindow window;window.show();return application.exec();","}","#endif"]
    return "\n".join(lines)+"\n"
