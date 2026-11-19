"""Offline Python-target compiler facts for the P01 experiment (no module execution)."""
import ast
import builtins
import dis
import types
import __future__
import symtable


def global_reads(code):
    for op in dis.get_instructions(code):
        if op.opname in {"LOAD_GLOBAL", "LOAD_FROM_DICT_OR_GLOBALS"}:
            yield op.argval
        elif op.opname in {"STORE_GLOBAL", "DELETE_GLOBAL"}:
            raise ValueError("global mutation")
    for child in code.co_consts:
        if isinstance(child, types.CodeType):
            yield from global_reads(child)


def module_facts(source):
    """Only plain module functions and explicit imports can be copied into a bundle."""
    nodes = ast.parse(source).body
    facts, snippets = {}, {}

    def register(name, node, kind="unsupported", **attrs):
        if name in facts:
            kind = "ambiguous"
        facts[name] = dict(line=node.lineno, kind=kind, refs=[], **attrs)

    for node in nodes:
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                if alias.name == "*":
                    raise ValueError("wildcard import")
                module = alias.name if isinstance(node, ast.Import) else node.module
                name = alias.asname or alias.name.split(".")[0]
                register(name, node, "import", module=module or "")
                if isinstance(node, ast.ImportFrom):
                    if node.level:
                        facts[name]["kind"] = "unsupported"
                    stmt = ast.ImportFrom(module=module, names=[alias], level=node.level)
                else:
                    stmt = ast.Import(names=[alias])
                snippets[name] = ast.unparse(stmt)
        elif isinstance(node, ast.FunctionDef):
            register(node.name, node, "function")
            snippets[node.name] = ast.get_source_segment(source, node)
            defaults = node.args.defaults + [v for v in node.args.kw_defaults if v]
            if node.decorator_list or any(not isinstance(v, ast.Constant) for v in defaults):
                facts[node.name]["kind"] = "unsupported"
                continue
            code = compile(ast.Module(body=[node], type_ignores=[]), "<target>", "exec",
                           flags=__future__.annotations.compiler_flag, dont_inherit=True)
            body = next(v for v in code.co_consts
                        if isinstance(v, types.CodeType) and v.co_name == node.name
                        and v.co_firstlineno == node.lineno)
            try:
                facts[node.name]["refs"] = sorted(set(global_reads(body)))
            except ValueError:
                facts[node.name]["kind"] = "unsupported"
        else:
            table = symtable.symtable(ast.unparse(node), "<target>", "exec")
            for symbol in table.get_symbols():
                if symbol.is_assigned() or symbol.is_imported():
                    register(symbol.get_name(), node)
    for entry in facts.values():
        entry["refs"] = [name for name in entry["refs"]
                         if name in facts or not hasattr(builtins, name)]
    return facts, snippets


def package(facts, snippets, name, line, resolver):
    """Return the exact definitions selected by the bounded native resolver."""
    import json
    import subprocess
    if name not in facts or facts[name]["line"] != line:
        raise ValueError("not a unique module-level definition at the requested line")
    request = dict(symbols=facts, root=name)
    result = json.loads(subprocess.check_output([resolver], input=json.dumps(request) + "\n",
                                                text=True))
    if "error" in result:
        raise ValueError(result["error"])
    names = result["names"]
    return names, "\n\n".join(snippets[n] for n in names)


if __name__ == "__main__":
    import argparse
    import json
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("function")
    parser.add_argument("line", type=int)
    parser.add_argument("--resolver", default="build/native/corpus_dependency_closure")
    args = parser.parse_args()
    try:
        names, body = package(*module_facts(args.source.read_text()), args.function,
                              args.line, args.resolver)
        print(json.dumps(dict(definitions=names, source="from __future__ import annotations\n"+body+"\n")))
    except (SyntaxError, ValueError) as exc:
        parser.exit(1, str(exc) + "\n")
