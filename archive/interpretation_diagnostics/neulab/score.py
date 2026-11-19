"""Independently check raw top-one snippets against the frozen cases."""
import ast
import builtins
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
CUTOFF = json.loads((HERE.parents[2] / "experiments/algorithm_ir/contract_trial/cases.json").read_text())


def expected(key):
    binary = [(3,5), (-2,4), (0,0)]
    if key in ("B01", "B02"):
        return [(dict(a=a,b=b), 2*a-b if key=="B01" else 2*(a-b)) for a,b in binary]
    if key in ("B03","B04"):
        return [({},5)]
    if key in ("M01","M02"):
        op = min if key=="M01" else max
        return [(dict(a=a,b=b),op(a,b)) for a,b in [(2,9),(9,2),(-3,-3)]]
    if key in ("B05","B06","B07","B08"):
        lists = [[-1,4,-2,3,5],[],[0,-3,6]] if key in ("B05","B06") else [[-4,3,2],[-2,-1],[]]
        answers = {"B05":[7,0,6],"B06":[4,0,0],"B07":[1,0,0],"B08":[5,0,0]}
        return [(dict(values=xs),y) for xs,y in zip(lists,answers[key])]
    if key in ("M03","M04","M05","M06","M07","M08"):
        lists, answers = {
            "M03": ([[],[4,-1,0],[-2]], [0,3,-2]),
            "M04": ([[],[4,-1,0],[-2]], [0,3,-2]),
            "M05": ([[],[2,-3,4],[0,3]], [[],[2,-6,-24],[0,0]]),
            "M06": ([[],[2,-3,4],[0,3]], [1,-24,0]),
            "M07": ([[],[0,3,0],[7]], [[],[1,3],[]]),
            "M08": ([[],[0,3,0],[7]], [[],[0,2],[]]),
        }[key]
        return [(dict(values=xs),y) for xs,y in zip(lists,answers)]
    if key in ("phrase1","phrase2","phrase3","phrase4","phrase5","named"):
        return [(dict(values=xs,cutoff=k), sum(x>k for x in xs)) for xs,k in CUTOFF]
    return []


def structure(key, tree):
    nodes = list(ast.walk(tree))
    funcs = [n for n in nodes if isinstance(n, ast.FunctionDef)]
    loops = (ast.For, ast.While)
    if key in ("S01","S02"):
        return any(len(n.args.args) == (2 if key=="S01" else 3) for n in funcs)
    if key in ("S03","S04"):
        return any(isinstance(n, ast.For) for n in nodes)
    if key == "S05":
        return any(sum(isinstance(n, loops) for n in f.body)>=2 for f in funcs)
    if key == "S06":
        return any(any(isinstance(m,loops) for m in list(ast.walk(n))[1:]) for f in funcs for n in ast.walk(f) if isinstance(n,loops))
    if key in ("S07","S08"):
        def has_loop(block):
            return any(isinstance(m,loops) for stmt in block for m in ast.walk(stmt))
        return any(has_loop(n.body)==(key=="S07") and has_loop(n.orelse) for n in nodes if isinstance(n,ast.If))
    if key in ("M01","M02"):
        return any(isinstance(n,(ast.If,ast.IfExp)) for n in nodes)
    if key in ("M03","M04","phrase1"):
        return any(isinstance(n,ast.For) and (key!="phrase1" or any(isinstance(m,ast.If) for m in ast.walk(n))) for f in funcs for n in ast.walk(f))
    if key.startswith("M"):
        return any(isinstance(n,loops) for n in nodes)
    return True


# Execute with no network or access to user files. One interpreter per snippet.
DRIVER = """
import ast,json,sys
code,rows=json.load(sys.stdin)
tree=ast.parse(code)
result=[]
for env,y in rows:
    args=list(env.values())
    try:
        if len(tree.body)==1 and isinstance(tree.body[0],ast.Expr):
            value=eval(compile(ast.Expression(tree.body[0].value),'snippet','eval'),env)
        else:
            exec(code,env)
            funcs=[n.name for n in tree.body if isinstance(n,ast.FunctionDef)]
            value=env[funcs[0]](*args) if len(funcs)==1 else None
        result.append(type(value)==type(y) and value==y)
    except Exception as e:
        result.append(type(e).__name__)
print(json.dumps(result))
"""
def run(code,rows):
    cmd=["bwrap","--unshare-all","--die-with-parent","--ro-bind","/usr","/usr","--symlink","usr/lib","/lib","--symlink","usr/lib","/lib64","--proc","/proc","--dev","/dev","--tmpfs","/tmp","--chdir","/tmp","/usr/bin/python3","-I","-c",DRIVER]
    p=subprocess.run(cmd,input=json.dumps([code,rows]),text=True,capture_output=True,timeout=6)
    return json.loads(p.stdout) if p.returncode==0 else ["execution error"]


def main():
    scores=[]
    for line in (HERE/"predictions.jsonl").read_text().splitlines():
        r=json.loads(line)
        key,code=r["id"],r["top"]
        s=dict(id=key)
        scores.append(s)
        if code is None:
            s["result"]="no hypothesis"
            continue
        try:
            tree=ast.parse(code)
            compile(tree,"snippet","exec")
        except SyntaxError:
            s["result"]="invalid Python"
            continue
        s["compiles"]=True
        if key.startswith("C"):
            s["result"]="code offered on control"
            continue
        if key=="upstream":
            rows=[(dict(var_1=xs),list(reversed(xs))) for xs in ([],[3],[2,3,1])]
            s["execution"]=run("list("+code+")",rows)
            s["result"]="pass" if all(x is True for x in s["execution"]) else "wrong behavior"
            continue
        if key=="S01" and isinstance(tree.body[0],ast.Expr) and isinstance(tree.body[0].value, ast.Lambda):
            # The fixture allows an arbitrary body/name. Record the declaration distinction.
            s["two_argument_lambda"]=len(tree.body[0].value.args.args)==2
            s["integer_call"]=run("isinstance(("+code+")(3, 5), int)",[({}, True)])
        rows=expected(key)
        if rows:
            s["execution"]=run(code,rows)
        if not structure(key,tree):
            s["result"]="missing requested structure"
        elif rows:
            s["result"]="pass" if all(x is True for x in s["execution"]) else "wrong behavior"
        else:
            s["result"]="inspect"
    (HERE/"scores.json").write_text(json.dumps(scores,indent=2)+"\n")
if __name__=="__main__":
    main()
