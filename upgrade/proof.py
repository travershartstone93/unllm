"""Check the engine's witnesses against source claims and the finite rule set."""
from __future__ import annotations
import ast
import math
from .model import Goal, Claim


def term(text: str):
    # Souffle's JSON proof leaves use relation("argument",...). Parse that
    # existing syntax as an AST and accept only a call with constant arguments.
    if text.startswith("!=("):
        text = "neq" + text[2:]
    node = ast.parse(text, mode="eval").body
    if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name) or node.keywords:
        raise ValueError("Malformed provenance relation")
    values = []
    for value in node.args:
        if not isinstance(value, ast.Constant) or not isinstance(value.value, (str, int)):
            raise ValueError("Nonconstant provenance argument")
        values.append(str(value.value))
    return node.func.id, values


class ProofChecker:
    def __init__(self, claims: list[Claim], relations: list[dict], complete: list[dict]):
        self.claims = {c.id: c for c in claims}
        self.transitive = {r["name"] for r in relations if r.get("transitive")}
        self.inverses = {(r["name"], r["inverse"]) for r in relations if r.get("inverse")}
        self.domains = {(r["name"],r.get("domain","entity")) for r in relations}
        self.ranges = {(r["name"],r.get("range","entity")) for r in relations}
        self.complete = {(c["predicate"], c["subject"]) for c in complete}
        self.used = set()
        self.steps = []

    def souffle(self, tree: dict, depth=0):
        if depth > 100:
            raise ValueError("Provenance depth exceeded")
        if "axiom" in tree:
            name, args = term(tree["axiom"])
            if name == "source" and len(args) == 4:
                identifier, p, s, o = args
                claim = self.claims.get(identifier)
                if not claim or claim.negative or (claim.predicate, claim.subject, claim.object) != (p, s, o):
                    raise ValueError("Souffle provenance references an invalid source")
                self.used.add(identifier)
            elif name == "transitive" and len(args) == 1:
                if args[0] not in self.transitive:
                    raise ValueError("Undeclared transitive relation")
            elif name == "inverse" and len(args) == 2:
                if tuple(args) not in self.inverses:
                    raise ValueError("Undeclared inverse relation")
            elif name in ("arg_domain","arg_range") and len(args)==2:
                if tuple(args) not in (self.domains if name=="arg_domain" else self.ranges):
                    raise ValueError("Undeclared argument type")
            elif name == "neq" and len(args) == 2 and args[0] != args[1]:
                pass
            else:
                raise ValueError("Unsupported provenance axiom: " + name)
            return name, args
        name, args = term(tree["premises"])
        children = [self.souffle(c, depth+1) for c in tree.get("children", [])]
        if name != "closure" or len(args) != 3:
            raise ValueError("Unsupported derived relation")
        p, s, o = args
        valid = False
        if len(children) == 1:
            if children[0][0] == "source":valid = children[0][1][1:] == args
            if children[0] == ("closure",["country",s,o]) and p=="located_in":valid = True
        elif len(children) == 2:
            inverse = next((v for k, v in children if k == "inverse"), None)
            closure = next((v for k, v in children if k == "closure"), None)
            if inverse and closure:
                valid = inverse == [closure[0], p] and closure[1:] == [o, s]
            for kind,values in children:
                if kind=="arg_domain" and closure:
                    valid |= p=="type" and values==[closure[0],o] and closure[1]==s
                if kind=="arg_range" and closure:
                    valid |= p=="type" and values==[closure[0],o] and closure[2]==s
            if all(k == "closure" for k, _ in children):
                values = [v for _, v in children]
                valid |= ([p, s, o] == ["type", s, o] and any(
                    a == ["type", s, b[1]] and b == ["subclass_of", a[2], o]
                    for a in values for b in values if a is not b))
                valid |= (p == "country" and ["located_in", s, o] in values
                          and ["type", o, "country"] in values)
                valid |= (p == "compatible" and any(a[0] == "produces" and a[1] == s
                          and b == ["accepts",o,a[2]] for a in values for b in values if a is not b))
        elif len(children) in (3, 4):
            transitive = next((v for k, v in children if k == "transitive"), None)
            links = [v for k, v in children if k == "closure"]
            if transitive == [p] and len(links) == 2:
                valid = any(a[0] == p and a[1] == s and b == [p, a[2], o]
                            for a in links for b in links if a is not b)
        if not valid:
            raise ValueError("Invalid Souffle derivation")
        self.steps.append({"engine": "souffle", "rule": tree.get("rule-number"), "goal": args})
        return "closure", args

    def witness(self, witness: dict, goal: Goal):
        name, args = witness["functor"], witness["args"]
        if name == "claim":
            claim = self.claims.get(args[0])
            if not claim or (claim.predicate, claim.subject, claim.object, claim.negative) != (
                    goal.predicate, goal.subject, goal.object, goal.negative):
                raise ValueError("Prolog source witness does not match the goal")
            self.used.add(claim.id)
        elif name == "background":
            if goal.negative or args != [goal.predicate, goal.subject, goal.object]:
                raise ValueError("Invalid background witness")
            if self.souffle(witness["tree"]) != ("closure", args):
                raise ValueError("Background provenance has a different conclusion")
        elif name == "rule":
            rule, premises = args
            if rule=="height_comparison" and goal.predicate=="taller_than" and len(premises)==2:
                a,b=[self.witness_goal(w) for w in premises]
                if (a.predicate,a.subject,a.negative,b.predicate,b.subject,b.negative)!=(
                        "height",goal.subject,False,"height",goal.object,False):
                    raise ValueError("Invalid height-comparison premises")
                self.witness(premises[0],a);self.witness(premises[1],b)
                x,y=float(a.object),float(b.object)
                if not all(math.isfinite(v) and abs(v)<=1e15 for v in (x,y)) or (x>y)==goal.negative:
                    raise ValueError("Invalid numeric comparison")
                self.steps.append({"engine":"prolog","rule":rule,"goal":goal.data()})
                return
            if goal.predicate != "blocked" or goal.object != "true":
                raise ValueError("Rule applied to the wrong proposition")
            if rule == "requirement_unavailable" and not goal.negative and len(premises) == 2:
                dep = self.witness_goal(premises[0])
                if dep.predicate != "depends_on" or dep.subject != goal.subject or dep.negative:
                    raise ValueError("Invalid dependency premise")
                self.witness(premises[0], dep)
                self.witness(premises[1], Goal("available", dep.object, "true", True))
            elif rule == "requirements_satisfied" and goal.negative:
                if ("depends_on", goal.subject) not in self.complete:
                    raise ValueError("Completeness is required for all-requirements reasoning")
                graph={}
                edges={}
                for claim in self.claims.values():
                    if claim.predicate=="depends_on" and not claim.negative:
                        graph.setdefault(claim.subject,set()).add(claim.object)
                        edges.setdefault(claim.subject,[]).append(claim)
                required=set()
                todo=list(graph.get(goal.subject,()))
                for claim in edges.get(goal.subject,()):self.used.add(claim.id)
                while todo:
                    node=todo.pop()
                    if node not in required:
                        required.add(node);todo.extend(graph.get(node,()))
                        for claim in edges.get(node,()):self.used.add(claim.id)
                witnessed=set()
                for premise in premises:
                    premise_goal = self.witness_goal(premise)
                    if premise_goal.predicate != "available" or premise_goal.negative:
                        raise ValueError("Invalid availability premise")
                    self.witness(premise, premise_goal)
                    witnessed.add(premise_goal.subject)
                if required!=witnessed:
                    raise ValueError("The availability proof omits a required dependency.")
                self.steps.append({"engine":"scope","completeness":{"predicate":"depends_on","subject":goal.subject}})
            else:
                raise ValueError("Unsupported Prolog rule")
            self.steps.append({"engine": "prolog", "rule": rule, "goal": goal.data()})
        else:
            raise ValueError("Unknown proof witness")

    def witness_goal(self, witness: dict) -> Goal:
        if witness["functor"] == "claim":
            claim = self.claims[witness["args"][0]]
            return Goal(claim.predicate, claim.subject, claim.object, claim.negative)
        if witness["functor"] == "background":
            p, s, o = witness["args"]
            return Goal(p, s, o)
        raise ValueError("A compound rule witness needs an explicit conclusion")

    def check(self, proofs: list[dict],goals=None,bindings=None) -> dict:
        self.used=set()
        self.steps=[]
        if goals is not None and len(proofs)!=len(goals):raise ValueError("The answer omits a query obligation")
        for index,proof in enumerate(proofs):
            if proof["functor"] != "proof" or len(proof["args"]) != 5:
                raise ValueError("Malformed Prolog answer proof")
            sign, p, s, o, witness = proof["args"]
            if sign not in ("pos","neg"):raise ValueError("Invalid proof polarity")
            if goals is not None:
                expected=goals[index]
                def bound(value):
                    if not value.startswith("?"):return value
                    result=(bindings or {}).get(value)
                    if not isinstance(result,str):raise ValueError("An answer binding is missing or unground")
                    return result
                if (p,s,o,sign=="neg")!=(expected.predicate,bound(expected.subject),bound(expected.object),expected.negative):
                    raise ValueError("The proof does not establish the query under its returned bindings")
            self.witness(witness, Goal(p, s, o, sign == "neg"))
        return {"valid": True, "claim_ids": sorted(self.used), "steps": self.steps}
