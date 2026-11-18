"""Lower the typed operation plan into Python ASTs, then use ast.unparse."""
import ast
import copy
from ..spec import Expr

COMMON = r'''
import math
import sys

def checked_number(value):
    if not math.isfinite(value) or abs(value) > 1e15:
        raise ValueError("number outside the finite +/-1e15 domain")
    return value

def parse_number(text):
    text = text.strip()
    if not text or "_" in text or any(c in text for c in "xXpP"):
        raise ValueError("expected a decimal number")
    return checked_number(float(text))

def checked_line(raw, index):
    if index > 100000:
        raise ValueError("input exceeds 100000 rows")
    line = raw.removesuffix("\n").removesuffix("\r")
    if len(line) > 16384 or any((ord(c)<32 and c!="\t") or ord(c)>=127 for c in line):
        raise ValueError("expected printable ASCII and tabs, at most 16384 characters")
    return line

def format_number(value):
    return format(checked_number(value), ".17g")
'''

MAIN = r'''
def main():
    try:
        arguments = sys.argv[1:]
        if len(arguments) > 1:
            raise ValueError("usage: program [input-file]")
        if arguments:
            with open(arguments[0], "r", encoding="ascii") as stream:
                output = process_data(stream)
        else:
            output = process_data(sys.stdin)
        for line in output:
            print(line)
        return 0
    except (OSError, ValueError, OverflowError) as error:
        print("error: " + str(error), file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
'''


def expression(expr: Expr):
    if expr.op == "variable": return ast.Name(id="x",ctx=ast.Load())
    if expr.op == "constant": return ast.Constant(value=expr.value)
    args=[expression(a) for a in expr.args]
    if expr.op in ("add","subtract","multiply"):
        return ast.BinOp(left=args[0],op={"add":ast.Add,"subtract":ast.Sub,"multiply":ast.Mult}[expr.op](),right=args[1])
    if expr.op in ("gt","ge","lt","le","eq"):
        return ast.Compare(left=args[0],ops=[{"gt":ast.Gt,"ge":ast.GtE,"lt":ast.Lt,"le":ast.LtE,"eq":ast.Eq}[expr.op]()],
                           comparators=[args[1]])
    if expr.op in ("and","or"):
        return ast.BoolOp(op=ast.And() if expr.op=="and" else ast.Or(),values=args)
    if expr.op in ("not","negate"):
        return ast.UnaryOp(op=ast.Not() if expr.op=="not" else ast.USub(),operand=args[0])
    if expr.op == "if":return ast.IfExp(test=args[0],body=args[1],orelse=args[2])
    raise ValueError("Unsupported Python expression")


def transform(program):
    body=[]
    for step in program.steps:
        if step.op in ("filter","map"):
            x=ast.Name(id="x",ctx=ast.Load())
            result=expression(step.expr)
            generator=ast.comprehension(target=ast.Name(id="x",ctx=ast.Store()),
                iter=ast.Name(id="values",ctx=ast.Load()),ifs=[result] if step.op=="filter" else [],is_async=0)
            value=x if step.op=="filter" else ast.Call(func=ast.Name(id="checked_number",ctx=ast.Load()),
                                                      args=[result],keywords=[])
            body.append(ast.Assign(targets=[ast.Name(id="values",ctx=ast.Store())],
                                   value=ast.ListComp(elt=value,generators=[generator])))
        else:
            snippets={
                "unique":"values = list(dict.fromkeys(values))",
                "sort":"values = sorted(values, reverse="+str(step.descending)+")",
                "reverse":"values = list(reversed(values))",
                "lowercase":"values = [line.lower() for line in values]",
                "strip":"values = [line.strip() for line in values]",
                "words":"values = [word for line in values for word in line.split()]",
            }
            body.extend(ast.parse(snippets[step.op]).body)
    body.append(ast.Return(value=ast.Name(id="values",ctx=ast.Load())))
    return ast.FunctionDef(name="transform",args=ast.arguments(posonlyargs=[],args=[ast.arg(arg="values")],
        kwonlyargs=[],kw_defaults=[],defaults=[]),body=body,decorator_list=[])


def reader(program):
    if program.input == "records":
        code=r'''
def read_input(stream):
    data = {}
    for index, raw in enumerate(stream, 1):
        if index > 100000:
            raise ValueError("input exceeds 100000 rows")
        try:
            line = checked_line(raw, index)
            if not line.strip():
                continue
            group, separator, value = line.partition("\t")
            group = group.strip()
            if not separator or not group or "\t" in value:
                raise ValueError("expected key TAB number")
            number = parse_number(value)
            data.setdefault(group, []).append(number)
        except ValueError as error:
            if not SKIP_INVALID:
                raise ValueError("line " + str(index) + ": " + str(error)) from error
    return data
'''
    else:
        code=r'''
def read_input(stream):
    data = []
    for index, raw in enumerate(stream, 1):
        if index > 100000:
            raise ValueError("input exceeds 100000 rows")
        try:
            line = checked_line(raw, index)
            if NUMERIC:
                if not line.strip():
                    continue
                data.append(parse_number(line))
            else:
                data.append(line)
        except ValueError as error:
            if not SKIP_INVALID:
                raise ValueError("line " + str(index) + ": " + str(error)) from error
    return data
'''
    tree=ast.parse(code)
    class Constants(ast.NodeTransformer):
        def visit_Name(self,node):
            if node.id=="NUMERIC":return ast.copy_location(ast.Constant(program.input=="numbers"),node)
            if node.id=="SKIP_INVALID":return ast.copy_location(ast.Constant(program.invalid=="skip"),node)
            return node
    return Constants().visit(tree).body


def renderer(program):
    output=program.output
    if output=="values":
        body="return [format_number(x) for x in values]" if program.input!="lines" else "return list(values)"
    elif output=="count":
        body="return [str(len(values))]"
    elif output=="frequencies":
        body=r'''
counts = {}
for value in values:
    if "\t" in value:
        raise ValueError("frequency keys cannot contain tabs")
    counts[value] = counts.get(value, 0) + 1
return ["value\tcount"] + [value + "\t" + str(counts[value]) for value in sorted(counts)]
'''
    else:
        empty='return ["0"]' if output=="sum" else 'raise ValueError("no numeric values remain")'
        body="if not values:\n    "+empty+"\n"
        if output in ("sum","mean","stats"):
            body+="total = 0.0\nfor value in values:\n    total += value\n"
        if output in ("min","stats"):body+="minimum = min(values)\n"
        if output in ("max","stats"):body+="maximum = max(values)\n"
        if output in ("mean","stats"):body+="average = checked_number(total / len(values))\n"
        if output=="stats":
            body+=r'''return [str(len(values)) + "\t" + "\t".join(format_number(x) for x in (total, minimum, maximum, average))]'''
        else:
            value={"sum":"total","mean":"average","min":"minimum","max":"maximum"}[output]
            body+="return [format_number("+value+")]"
    tree=ast.parse("def render_values(values):\n"+ "\n".join("    "+line for line in body.strip().splitlines())+"\n")
    class Output(ast.NodeTransformer):
        def visit_Name(self,node):
            return ast.copy_location(ast.Constant(output),node) if node.id=="OUTPUT" else node
    return Output().visit(tree).body


def emit(program):
    module=ast.parse(COMMON)
    module.body.extend(reader(program))
    module.body.append(transform(program))
    module.body.extend(renderer(program))
    body=ast.parse("data = read_input(stream)").body
    if program.input=="records":
        if program.output=="stats":
            body.extend(ast.parse('output = ["key\\tcount\\tsum\\tmin\\tmax\\tmean"]').body)
        else:
            body.extend(ast.parse("output = []").body)
        body.extend(ast.parse(r'''
for group in sorted(data):
    values = transform(data[group])
    if not values:
        continue
    for row in render_values(values):
        output.append(group + "\t" + row)
return output
''').body)
    else:
        body.extend(ast.parse("values = transform(data)").body)
        if program.output=="stats":
            body.extend(ast.parse('return ["count\\tsum\\tmin\\tmax\\tmean"] + render_values(values)').body)
        else:
            body.extend(ast.parse("return render_values(values)").body)
    module.body.append(ast.FunctionDef(name=program.name,
        args=ast.arguments(posonlyargs=[],args=[ast.arg(arg="stream")],kwonlyargs=[],kw_defaults=[],defaults=[]),
        body=body,decorator_list=[]))
    main=ast.parse(MAIN)
    class Rename(ast.NodeTransformer):
        def visit_Name(self,node):
            return ast.copy_location(ast.Name(id=program.name,ctx=node.ctx),node) if node.id=="process_data" else node
    module.body.extend(Rename().visit(main).body)
    class Fold(ast.NodeTransformer):
        def visit_If(self,node):
            node=self.generic_visit(node)
            value=node.test
            if isinstance(value,ast.Constant) and type(value.value) is bool:
                return node.body if value.value else node.orelse
            if isinstance(value,ast.UnaryOp) and isinstance(value.op,ast.Not) and isinstance(value.operand,ast.Constant) and type(value.operand.value) is bool:
                return node.orelse if value.operand.value else node.body
            return node
        def visit_ExceptHandler(self,node):
            node=self.generic_visit(node)
            if not node.body:node.body=[ast.Pass()]
            return node
    module=Fold().visit(module)
    ast.fix_missing_locations(module)
    compile(module,"<synthesized>","exec")
    return ast.unparse(module)+"\n"
