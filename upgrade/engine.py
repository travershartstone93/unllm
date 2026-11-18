"""Session/context coordinator. Logical answers come from the native engines."""
from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
import copy
import json
import shlex
import time
from .model import Ambiguous, Claim, Goal, Meaning, Unsupported, digest, key
from .native import NativeEngine
from .proof import ProofChecker
from .semantics import SemanticParser
from .store import FactStore
from .linking import link


@dataclass
class Context:
    facts: dict[str, Claim] = field(default_factory=dict)
    labels: dict[str, str] = field(default_factory=dict)
    focus: list[str] = field(default_factory=list)
    previous: Meaning | None = None
    undo: list[dict] = field(default_factory=list)
    last_spec: dict | None = None
    last_language: str = "python"
    complete: list[dict] = field(default_factory=list)
    touched: float = field(default_factory=time.monotonic)


class Engine:
    def __init__(self, database: Path):
        self.store = FactStore(database)
        self.native = NativeEngine()
        self.parser = SemanticParser()
        self.contexts: dict[str, Context] = {}
        self.owns_read_snapshot=False

    def context(self, name):
        now = time.monotonic()
        self.contexts = {k: v for k, v in self.contexts.items() if now-v.touched < 1800}
        if name not in self.contexts and len(self.contexts) >= 128:
            oldest = min(self.contexts, key=lambda k: self.contexts[k].touched)
            del self.contexts[oldest]
        result = self.contexts.setdefault(name, Context())
        result.touched = now
        return result

    def interpret(self, text, context, identifier, *, programming=False):
        def resolve(label, allow_new=False):
            matches = self.store.resolve(label)
            local = [i for i, name in context.labels.items() if key(name) == key(label)]
            identities = sorted({r["id"] for r in matches} | set(local))
            if len(identities) > 1:
                raise Ambiguous("The name identifies multiple entities: " + label)
            if identities:
                return identities[0]
            if allow_new:
                return "session:" + digest([identifier, key(label)])[:24]
            raise Unsupported("I have no unique structured entity named " + label + ".")
        if programming:return self.parser.parse(text,resolve,context.focus,programming=True)
        aliases=[r[0] for r in self.store.db.execute(
            "SELECT a.alias FROM aliases a JOIN entities e ON e.id=a.entity WHERE e.type!='type'")]
        linked,names=link(text,[*aliases,*context.labels.values()])
        if names:
            def linked_resolve(label,allow_new=False):
                return resolve(names.get(key(label),label),allow_new)
            try:
                result=self.parser.parse(linked,linked_resolve,context.focus)
                result.labels={i:names.get(key(v),v) for i,v in result.labels.items()}
                result.diagnostics.append("registered entity names linked before grammar analysis")
                return result
            except Unsupported as original:
                try:return self.parser.parse(text,resolve,context.focus)
                except (Unsupported,Ambiguous):raise original
        return self.parser.parse(text,resolve,context.focus)

    def label(self, identifier, context):
        if identifier == "true":
            return "true"
        if identifier in context.labels:
            return context.labels[identifier]
        matches = self.store.resolve(identifier)
        return matches[0]["name"] if matches else identifier

    def sentence(self, goal: Goal, context) -> str:
        s, o = self.label(goal.subject, context), self.label(goal.object, context)
        no = "not " if goal.negative else ""
        if goal.predicate == "located_in":
            return f"{s} is {no}in {o}"
        if goal.predicate == "contains":
            return f"{s} {'does not contain' if goal.negative else 'contains'} {o}"
        if goal.predicate == "depends_on":
            return f"{s} {'does not depend' if goal.negative else 'depends'} on {o}"
        if goal.predicate in ("available", "blocked"):
            return f"{s} is {no}{goal.predicate}"
        if goal.predicate == "author":
            return f"{o} is {no}the author of {s}"
        if goal.predicate == "authored":
            return f"{s} {'did not write' if goal.negative else 'wrote'} {o}"
        if goal.predicate == "birthplace":
            return f"{s} was {no}born in {o}"
        if goal.predicate == "taller_than":
            return f"{s} is {no}taller than {o}"
        if goal.predicate == "height":
            return f"{s}'s height is {no}{o} metres"
        if goal.predicate == "type":
            return f"{s} is {no}a {o}"
        if goal.predicate == "subclass_of":
            return f"{s} is {no}a kind of {o}"
        return f"{s}: {goal.predicate.replace('_',' ')} {'is not' if goal.negative else 'is'} {o}"

    def assert_goals(self, meaning, context, *, correction=False, hypothetical=False):
        context.undo.append(dict(context.facts))
        context.undo = context.undo[-20:]
        if correction:
            targets = {(g.predicate, g.subject) for g in meaning.goals}
            context.facts = {i: f for i, f in context.facts.items()
                             if (f.predicate, f.subject) not in targets}
        for goal in meaning.goals:
            if goal.predicate not in {r["name"] for r in self.store.relations()}:
                raise Unsupported("Undeclared relation: " + goal.predicate)
            identifier = "session-claim:" + digest([goal.data(), len(context.undo)])
            context.facts[identifier] = Claim(identifier, goal.predicate, goal.subject, goal.object,
                                             goal.negative, source="conversation",
                                             origin="hypothetical" if hypothetical else "session")
        context.labels.update(meaning.labels)
        context.focus = sorted({g.subject for g in meaning.goals if not g.subject.startswith("?")})

    def evaluate(self, meaning, context, now, assumptions=()):
        claims = [*self.store.claims(now), *context.facts.values()]
        if assumptions:
            overridden = {(g.predicate, g.subject, g.object) for g in assumptions}
            claims = [c for c in claims if (c.predicate, c.subject, c.object) not in overridden]
            claims += [Claim("assumption:" + digest(g.data()),g.predicate,g.subject,g.object,g.negative,
                             source="hypothesis",origin="hypothetical") for g in assumptions]
        facts = [c.data() for c in claims]
        relations, complete = self.store.relations(), self.store.completeness()+context.complete
        prepared=self.store.prepared(self.native.input_key(facts,relations))
        result = self.native.query(facts, relations, complete, [g.data() for g in meaning.goals],prepared=prepared)
        checked, seen = [], set()
        checker=ProofChecker(claims,relations,complete)
        claim_by_id={c.id:c for c in claims}
        for row in result["solutions"]:
            signature = digest(row["bindings"])
            if signature in seen:
                continue
            proof = checker.check(row["proofs"],meaning.goals,row["bindings"])
            checked.append({**row, "derivation": proof})
            seen.add(signature)
        result["solutions"] = checked
        result["completeness"]=complete
        result["sources"] = {}
        for row in checked:
            for identifier in row["derivation"]["claim_ids"]:
                claim = claim_by_id[identifier]
                source = self.store.source(claim.source) if claim.origin in ("source", "metadata") else {
                    "id": claim.source, "title": "Hypothetical assumption" if claim.origin == "hypothetical"
                    else "User assertion in this conversation"}
                result["sources"][identifier] = {"claim": claim.data(), "source": source}
        result["meaning"] = {"act": meaning.act, "goals": [g.data() for g in meaning.goals],
                             "select": meaning.select, "count":meaning.count,"diagnostics": meaning.diagnostics}
        return result

    def answer(self, meaning, context, now, assumptions=()):
        result = self.evaluate(meaning, context, now, assumptions)
        if result["status"] != "complete":
            return {**result, "text": "Reasoning reached its resource budget; this is not a negative answer."}
        positive = bool(result["solutions"])
        if meaning.select:
            answers = []
            for row in result["solutions"]:
                values = [row["bindings"].get(v, v) for v in meaning.select]
                if any(v is None for v in values):
                    continue
                if values not in answers:
                    answers.append(values)
            if not answers:
                result.update(status="unknown", truth="unknown", answers=[],
                              text="I have no supported match in the structured knowledge.")
            else:
                displays = [", ".join(self.label(v, context) for v in row) for row in answers]
                result.update(truth="supported", answers=answers, complete_set=False)
                if meaning.count:
                    result["known_count"]=len(answers)
                    result["text"] = f"I know {len(answers)} matching results. The knowledge does not establish an exhaustive count."
                else:
                    result["text"] = "; ".join(displays) + "."
                context.focus = list(dict.fromkeys(v for row in answers for v in row))
        else:
            negative = False
            if len(meaning.goals) == 1:
                goal = meaning.goals[0]
                reverse = Meaning("query",[Goal(goal.predicate,goal.subject,goal.object,not goal.negative)])
                opposite = self.evaluate(reverse, context, now, assumptions)
                if opposite["status"] != "complete":
                    return {**opposite, "text": "The opposite-evidence search exceeded its budget."}
                negative = bool(opposite["solutions"])
                result["opposite_solutions"] = opposite["solutions"]
                result["sources"].update(opposite["sources"])
            truth = "both" if positive and negative else "supported" if positive else "refuted" if negative else "unknown"
            result["truth"] = truth
            if truth == "both":
                result["text"] = "The evidence conflicts: I can support both the statement and its negation."
            elif truth == "supported":
                result["text"] = "Yes. " + "; ".join(self.sentence(g, context) for g in meaning.goals) + "."
            elif truth == "refuted":
                result["text"] = "No. There is explicit evidence for the opposite statement."
            else:
                result.update(status="unknown", text="I cannot establish this from the available premises.")
                if len(meaning.goals) == 1 and meaning.goals[0].predicate == "blocked":
                    goal = meaning.goals[0]
                    dependencies = self.evaluate(Meaning("query",[Goal("depends_on",goal.subject,"?dependency")],
                                                        ["?dependency"]), context, now, assumptions)
                    unknown = []
                    for row in dependencies["solutions"]:
                        dep = row["bindings"]["?dependency"]
                        available = self.evaluate(Meaning("query",[Goal("available",dep,"true")]),context,now,assumptions)
                        unavailable = self.evaluate(Meaning("query",[Goal("available",dep,"true",True)]),context,now,assumptions)
                        if not available["solutions"] and not unavailable["solutions"]:
                            unknown.append(dep)
                    if unknown:
                        result["missing_premises"] = [Goal("available",d,"true").data() for d in unknown]
                        result["text"] += " Availability is unknown for: " + ", ".join(self.label(d,context) for d in unknown) + "."
            context.focus = list(dict.fromkeys(g.subject for g in meaning.goals if not g.subject.startswith("?")))
        if meaning.explain and result.get("sources"):
            premises = []
            for entry in result["sources"].values():
                c = entry["claim"]
                phrase = self.sentence(Goal(c["predicate"],c["subject"],c["object"],c["negative"]),context)
                if phrase not in premises:
                    premises.append(phrase)
            result["text"] += " Premises: " + "; ".join(premises) + "."
        if assumptions:
            result["hypothetical"] = True
            result["assumptions"] = [g.data() for g in assumptions]
            result["text"] = "Under that assumption: " + result["text"]
        return result

    def ask(self, request: dict):
        owns=not self.store.db.in_transaction
        previous=self.owns_read_snapshot
        if owns:self.store.db.execute("BEGIN")
        self.owns_read_snapshot=owns
        try:return self.ask_snapshot(request)
        finally:
            if owns and self.store.db.in_transaction:self.store.db.rollback()
            self.owns_read_snapshot=previous

    def ask_snapshot(self, request: dict):
        self.parser.last_ms=0.0
        now = int(request.get("now") or time.time())
        identifier = str(request.get("context", "default"))
        context = self.context(identifier)
        text = request.get("question", "").strip()
        if text.startswith("{"):
            structured = json.loads(text)
            if "input" in structured or "program" in structured:
                request = {**request, "spec": structured}
            else:
                request = {**structured, "context": identifier, "now": now}
        if "spec" in request:
            return self.synthesize(request, context)
        if text.startswith(":code "):
            if not context.last_spec:
                return {"status":"unknown","text":"Generate a program first."}
            return self.synthesize({**request,"spec":context.last_spec,"language":text.split(maxsplit=1)[1]},context)
        if text.startswith(":revise "):
            if not context.last_spec:
                return {"status":"unknown","text":"Generate a program first."}
            changes = json.loads(text.split(maxsplit=1)[1])
            if not isinstance(changes,dict):
                raise Unsupported("A revision must be an object of specification fields.")
            return self.synthesize({**request,"spec":{**context.last_spec,**changes},"language":context.last_language},context)
        if text.startswith(":complete "):
            parts=shlex.split(text)
            if len(parts)!=3:
                raise Unsupported('Use :complete RELATION "entity name".')
            relation=parts[1]
            if relation not in {r["name"] for r in self.store.relations()}:
                raise Unsupported("Unknown relation.")
            name=parts[2]
            matches=self.store.resolve(name)
            identities={r["id"] for r in matches}|{i for i,label in context.labels.items() if key(label)==key(name)}
            if len(identities)!=1:raise Ambiguous("The completeness declaration needs one known entity.")
            item={"predicate":relation,"subject":next(iter(identities)),"source":"conversation"}
            if item not in context.complete:context.complete.append(item)
            return {"status":"complete","text":"Recorded a completeness boundary for this conversation.",
                    "complete":context.complete}
        if request.get("language") and not text.startswith(":"):
            return self.synthesize(request,context)
        if request.get("act") == "query":
            goals = [Goal(**g) for g in request["goals"]]
            meaning = Meaning("query", goals, request.get("select", []), explain=request.get("explain",False))
        else:
            if text == ":reset":
                self.contexts[identifier] = Context()
                return {"status":"complete","text":"Conversation facts and assumptions cleared."}
            if text == ":undo":
                if not context.undo:
                    return {"status":"unknown","text":"There is no assertion to undo."}
                context.facts = context.undo.pop()
                return {"status":"complete","text":"The previous assertion was undone."}
            if text in (":why", ":evidence", ":missing"):
                if not context.previous:
                    return {"status":"unknown","text":"Ask a factual question first."}
                meaning = copy.deepcopy(context.previous)
                meaning.explain = True
            else:
                correction = False
                for marker in (":correct ", "no, ", "actually, "):
                    if text.casefold().startswith(marker):
                        text = text[len(marker):]
                        correction = True
                        break
                meaning = self.interpret(text, context, identifier)
                if meaning.act == "synthesis":
                    return self.synthesize({**request,"semantic_meaning":meaning},context)
                if meaning.act == "explain":
                    if not context.previous:
                        return {"status":"unknown","text":"Ask a factual question first."}
                    meaning = copy.deepcopy(context.previous)
                    meaning.explain = True
                if meaning.act == "assert":
                    self.assert_goals(meaning,context,correction=correction)
                    return {"status":"complete","truth":"asserted","text":
                            ("Corrected: " if correction else "Recorded for this conversation: ") +
                            "; ".join(self.sentence(g,context) for g in meaning.goals) + ".",
                            "claims":[c.data() for c in context.facts.values()]}
        if meaning.act == "hypothetical":
            if not meaning.goals:
                if not context.previous:
                    return {"status":"unknown","text":"Ask a question before changing its assumptions."}
                goals = copy.deepcopy(context.previous)
            else:
                goals = Meaning("query",meaning.goals,meaning.select,explain=meaning.explain)
            # A hypothetical query never mutates the stored conversation.
            temporary = copy.deepcopy(context)
            return self.answer(goals,temporary,now,meaning.assumptions)
        context.previous = copy.deepcopy(meaning)
        return self.answer(meaning,context,now)

    def synthesize(self, request, context):
        # Code construction does not consume the factual store. Do not retain
        # a read snapshot for a potentially long compilation/validation job.
        if self.owns_read_snapshot and self.store.db.in_transaction:self.store.db.rollback()
        from .synthesis import synthesize
        return synthesize(request, context, self)
