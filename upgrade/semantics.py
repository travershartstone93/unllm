"""ERG MRS -> a deliberately bounded logical fragment.

This translates predicate/argument structures, not sentence templates. Content
that cannot be given a meaning is rejected instead of silently dropped.
"""
from __future__ import annotations

from pathlib import Path
import copy
import os
import subprocess
import time
from delphin import ace
from .model import Ambiguous, Goal, Meaning, TYPE_WORDS, Unsupported, key

ROOT = Path(__file__).resolve().parents[1]


def lemma(predicate: str) -> str:
    parts = predicate.split("_")
    return parts[1] if predicate.startswith("_") and len(parts) > 2 else predicate


class SemanticParser:
    def __init__(self, root: Path = ROOT, max_parses: int = 64):
        self.root = root
        self.max_parses = max_parses
        self.process = None
        self.fragment_process = None
        self.program_process = None
        self.last_ms = 0.0

    def close(self):
        for process in (self.process,self.fragment_process,self.program_process):
            if not process:continue
            process.close()
            # PyDelphin 1.11 closes stdin and waits, but leaves stdout open.
            if process._p.stdout:process._p.stdout.close()
        self.process=self.fragment_process=self.program_process=None

    def new_process(self,root,timeout=1):
        return ace.ACEParser(str(self.root/".deps/erg.dat"),
            executable=str(self.root/".deps/bin/ace"),
            cmdargs=["-n",str(self.max_parses),"-r",root,"--timeout="+str(timeout)],stderr=subprocess.DEVNULL)

    def parse(self, text: str, resolve, focus: list[str] | None = None, *, programming=False) -> Meaning:
        if len(text) > 4000:
            raise Unsupported("The question exceeds the supported language input budget.")
        if any(c in text for c in ("\n","\r","\0")):
            raise Unsupported("The grammar accepts one sentence per request.")
        annotations=[]
        from synthesis.behaviour.lexicon import normalize,tokens
        first=tokens(text)
        program_intent=programming or bool(first and first[0][0] in ("write","create","generate","build","make"))
        if program_intent:
            text,annotations=normalize(text)
        if programming:
            if self.program_process is None:self.program_process=self.new_process("root_informal",timeout=4)
            process=self.program_process
        else:
            if self.process is None:self.process=self.new_process("root_informal")
            process=self.process
        start = time.monotonic()
        response = process.interact(text)
        results = response.results()
        fragment=False
        if not results:
            # Full sentences must not compete with the grammar's enormous
            # fragment forest for the bounded set of returned readings.
            # A separate lazy fragment parser retains follow-ups such as Why?
            if self.fragment_process is None:self.fragment_process=self.new_process("root_inffrag")
            results=self.fragment_process.interact(text).results()
            fragment=True
        self.last_ms += (time.monotonic() - start) * 1000
        if not results:
            raise Unsupported("The grammar could not analyze this request within its budget.")
        candidates = {}
        programs = []
        errors = []
        for result in results:
            try:
                meaning = translate(result.mrs(), text, resolve, focus or [],programming=programming)
                if meaning.act == "synthesis":
                    from synthesis.behaviour.lexicon import attach
                    attach(meaning.semantics,annotations)
                    programs.append(meaning)
                    continue
                candidates.setdefault(meaning.signature(), meaning)
            except (Unsupported, Ambiguous) as exc:
                errors.append(exc)
        if programs:
            meaning = programs[0]
            meaning.analyses = [p.semantics for p in programs]
            meaning.diagnostics.append(f"ACE/ERG; {len(programs)} program analyses; zero ranking weights")
            meaning.diagnostics.append("Explicit code grammar budget: 4 seconds" if programming else "Normal grammar budget: 1 second")
            if annotations:meaning.diagnostics.append("Domain lexical linking: "+str(annotations))
            return meaning
        if not candidates:
            if errors:raise errors[0]
            raise Unsupported("No supported interpretation.")
        if len(candidates) > 1:
            raise Ambiguous("The grammar found different supported meanings. Please make the relation or reference explicit.")
        meaning = next(iter(candidates.values()))
        meaning.diagnostics.extend([
            f"ACE/ERG; zero parse-ranking weights; {len(results)} analyses",
            f"semantic translation accepted {len(candidates)} distinct meaning",
        ])
        if fragment:meaning.diagnostics.append("fragment grammar used after no full-sentence parse")
        if len(results) >= self.max_parses:
            # The caller can expose this limitation and avoid claiming that all
            # grammatical interpretations have been enumerated.
            meaning.diagnostics.append("parse enumeration reached its configured bound")
        return meaning


def translate(mrs, text: str, resolve, focus: list[str], *, programming=False) -> Meaning:
    eps = list(mrs.rels)
    if not eps:
        raise Unsupported("Empty grammatical analysis.")
    variables = mrs.variables
    wh = {e.args["ARG0"] for e in eps
          if e.predicate in ("which_q", "_which_q") and "ARG0" in e.args}
    how_events={e.args.get("ARG1") for e in eps if e.predicate=="_how_x_comp"}
    counted={e.args["ARG1"] for e in eps if e.predicate=="much-many_a" and e.args.get("ARG0") in how_events}
    wh |= counted
    why = any(e.predicate == "reason" for e in eps)
    if why:
        wh -= {e.args["ARG0"] for e in eps if e.predicate == "reason"}
    force = variables.get(mrs.index, {}).get("SF", "prop")
    is_question = force == "ques" or bool(wh) or why or text.rstrip().endswith("?")
    predicates = {e.predicate for e in eps}
    # Imperatives about programs are routed to the typed synthesis translator,
    # which also consumes these MRS predicate/argument structures.
    creation=any(lemma(e.predicate) in ("write","create","generate","build","make") and "_v_" in e.predicate for e in eps)
    if programming or force == "comm" and creation:
        meaning = Meaning("synthesis")
        handles={h.hi:h.lo for h in mrs.hcons}
        def descendants(handle):
            result=set();todo=[handle]
            while todo:
                label=todo.pop()
                if label in result:continue
                result.add(label)
                if label in handles:todo.append(handles[label])
                for related in eps:
                    if related.label==label:
                        todo.extend(v for k,v in related.args.items() if k!="ARG0" and isinstance(v,str) and v.startswith("h"))
            return sorted(result)
        meaning.semantics = [{"predicate": e.predicate, "label": e.label,
                              "args": dict(e.args), "carg": e.carg,
                              "span": e.lnk.data if e.lnk else None,
                              "scope":[label for k,v in e.args.items() if k!="ARG0" and isinstance(v,str) and v.startswith("h")
                                       for label in descendants(v)]} for e in eps]
        return meaning
    if any(lemma(e.predicate) in ("every", "all", "no", "neither", "or") for e in eps):
        raise Unsupported("This quantified or disjunctive interpretation needs a completeness/scope model not yet supported.")

    hq = {h.hi: h.lo for h in mrs.hcons}

    def scope(handle):
        seen, todo = set(), [handle]
        while todo:
            h = todo.pop()
            if h in seen:
                continue
            seen.add(h)
            if h in hq:
                todo.append(hq[h])
            for ep in eps:
                if ep.label == h:
                    todo.extend(v for k, v in ep.args.items()
                                if k != "ARG0" and isinstance(v, str) and v.startswith("h"))
        return seen

    negatives = [scope(e.args["ARG1"]) for e in eps if e.predicate == "neg"]
    assumption_scope = set()
    conditional = [e for e in eps if e.predicate in ("_if_x", "_if_x_then","_how+about_x")]
    if len(conditional) > 1:
        raise Unsupported("Nested conditional scope is not supported.")
    if conditional:
        ep = conditional[0]
        handle = ep.args.get("ARG1") if ep.predicate in ("_if_x_then","_how+about_x") else ep.args.get("ARG2")
        assumption_scope = scope(handle)

    # Union only grammatical identity, never similarity of words.
    parent = {}
    def find(x):
        parent.setdefault(x, x)
        if parent[x] != x:
            parent[x] = find(parent[x])
        return parent[x]
    def union(a, b):
        if find(a) != find(b):
            parent[find(b)] = find(a)

    names = {}
    nouns = {}
    for ep in eps:
        var = ep.args.get("ARG0")
        if ep.predicate == "named" and ep.carg:
            names.setdefault(var, []).append((ep.lnk.data[0] if ep.lnk else 0,ep.carg))
        if "_n_" in ep.predicate:
            nouns.setdefault(var, []).append(lemma(ep.predicate))

    owners = {}
    def owner_of(entity,owner):
        if entity in owners and owners[entity]!=owner:
            raise Unsupported("A relational noun has incompatible argument attachments.")
        owners[entity]=owner
    for ep in eps:
        if ep.predicate == "_of_p":
            owner_of(ep.args.get("ARG1"),ep.args.get("ARG2"))
        elif ep.predicate == "poss":
            owner_of(ep.args.get("ARG1"),ep.args.get("ARG2"))
    for ep in eps:
        if "_n_" in ep.predicate and ep.args.get("ARG1", "").startswith("x"):
            owner_of(ep.args["ARG0"],ep.args["ARG1"])

    raw_goals = []  # (predicate, subject variable, object variable/literal, ep)
    consumed = set()
    for ep in eps:
        if ep.predicate=="compound" and ep.args.get("ARG1") in names and ep.args.get("ARG2") in names:
            union(ep.args["ARG1"],ep.args["ARG2"])
            consumed.add(id(ep))
    type_literals = set()
    for ep in eps:
        if ep.predicate == "_be_v_id":
            a, b = ep.args.get("ARG1"), ep.args.get("ARG2")
            btypes = [TYPE_WORDS[w] for w in nouns.get(b, []) if w in TYPE_WORDS]
            atypes = [TYPE_WORDS[w] for w in nouns.get(a, []) if w in TYPE_WORDS]
            if btypes and b not in owners and b not in names and a not in wh:
                raw_goals.append(("type", a, "@" + btypes[0], ep))
                type_literals.add(b)
            elif atypes and a not in owners and a not in names and b not in wh:
                raw_goals.append(("type", b, "@" + atypes[0], ep))
                type_literals.add(a)
            else:
                union(a, b)
            consumed.add(id(ep))

    for ep in eps:
        pred, args = ep.predicate, ep.args
        word = lemma(pred)
        a0, a1, a2 = args.get("ARG0"), args.get("ARG1"), args.get("ARG2")
        if pred in ("_in_p_loc", "loc_nonsp"):
            # Where was X born? loc_nonsp attaches to the birth event.
            birth = next((e for e in eps if e.args.get("ARG0") == a1
                          and lemma(e.predicate) in ("born", "bear")), None)
            if birth and "_v_" in birth.predicate:
                if birth.args.get("ARG1","").startswith("x"):
                    raise Unsupported("The explicit birth-event agent cannot be represented.")
                birth_subject=birth.args.get("ARG2")
            else:
                birth_subject=birth.args["ARG1"] if birth else a1
            raw_goals.append(("birthplace" if birth else "located_in",birth_subject,a2,ep))
            if birth:
                consumed.add(id(birth))
            consumed.add(id(ep))
        elif "_v_" in pred and word in ("depend", "require", "need"):
            if a2 and a2.startswith("x"):
                raw_goals.append(("depends_on", a1, a2, ep)); consumed.add(id(ep))
        elif "_v_" in pred and word == "write":
            raw_goals.append(("authored", a1, a2, ep)); consumed.add(id(ep))
        elif "_v_" in pred and word == "contain":
            raw_goals.append(("contains", a1, a2, ep)); consumed.add(id(ep))
        elif word == "available" and "_a_" in pred:
            raw_goals.append(("available", a1, "@true", ep)); consumed.add(id(ep))
        elif word in ("blocked", "block") and ("_a_" in pred or "_v_" in pred):
            subject = a2 if "_v_" in pred else a1
            if "_v_" in pred and a1 and a1.startswith("x"):
                raise Unsupported("An explicit blocking agent is outside the availability rule.")
            raw_goals.append(("blocked", subject, "@true", ep)); consumed.add(id(ep))
        elif "_n_" in pred and word in ("author", "birthplace", "country", "capital", "height", "population"):
            owner = owners.get(a0)
            if owner:
                raw_goals.append((word, owner, a0, ep)); consumed.add(id(ep))
        elif word in ("born", "bear") and any(g[0] == "birthplace" for g in raw_goals):
            consumed.add(id(ep))

    for comparison in eps:
        if comparison.predicate!="more_comp":continue
        adjective=next((e for e in eps if e.args.get("ARG0")==comparison.args.get("ARG1")
                        and e.predicate=="_tall_a_1"),None)
        other=comparison.args.get("ARG2","")
        if adjective and other.startswith("x"):
            raw_goals.append(("taller_than",adjective.args.get("ARG1"),other,comparison))
            consumed.update((id(adjective),id(comparison)))

    # Attach known type restrictions to variables, including wh answers.
    for var, words in nouns.items():
        if var in type_literals:
            for ep in eps:
                if ep.args.get("ARG0")==var and "_n_" in ep.predicate and lemma(ep.predicate) in TYPE_WORDS:
                    consumed.add(id(ep))
            continue
        for word in words:
            if word in TYPE_WORDS and TYPE_WORDS[word] != "entity":
                if not any(id(ep) in consumed and ep.args.get("ARG0") == var for ep in eps):
                    raw_goals.append(("type", var, "@" + TYPE_WORDS[word],
                                      next(ep for ep in eps if ep.args.get("ARG0") == var and "_n_" in ep.predicate)))
                for ep in eps:
                    if ep.args.get("ARG0") == var and lemma(ep.predicate) == word:
                        consumed.add(id(ep))

    structural = {"named", "proper_q", "pronoun_q", "pron", "which_q", "_which_q",
                  "udef_q", "def_explicit_q", "def_implicit_q", "_the_q", "_a_q", "_an_q",
                  "thing", "person", "place_n", "reason", "neg", "_be_v_id", "_of_p", "poss",
                  "_and_c", "implicit_conj", "_if_x", "_if_x_then", "_how+about_x", "unknown"}
    for ep in eps:
        if id(ep) in consumed or ep.predicate in structural:
            continue
        if ep.predicate == "_for_p" and why:
            continue
        if ep.predicate == "_on_p_state" and any(g[0] == "depends_on" for g in raw_goals):
            continue
        if ep.predicate=="generic_verb" and ep.label in assumption_scope and any(
                e.predicate=="_how+about_x" for e in conditional) and set(ep.args)=={"ARG0","ARG1"}:
            continue
        # Counting is allowed only as a count of known matches. Completeness is
        # decided by the knowledge layer, never inferred from the wording.
        if ep.predicate in ("_how_x_comp", "much-many_a", "generic_entity"):
            if ep.predicate!="generic_entity" and not counted:
                raise Unsupported("An unbounded quantity needs an explicit scope model.")
            continue
        raise Unsupported("Unsupported semantic content: " + ep.predicate)

    if not raw_goals:
        if why:
            return Meaning("explain", explain=True)
        raise Unsupported("The analysis contains no supported relation.")

    canonical_names = {}
    for var, labels in names.items():
        root = find(var)
        canonical_names.setdefault(root, []).extend(labels)
    bound = {}
    labels = {}
    for var, parts in canonical_names.items():
        label = " ".join(label for _,label in sorted(parts))
        identity = resolve(label, not is_question)
        bound[var] = identity
        labels[identity] = label
    for ep in eps:
        if ep.predicate == "pron" and variables.get(ep.args["ARG0"], {}).get("PERS") == "3":
            if len(set(focus)) != 1:
                raise Ambiguous("The pronoun has no unique referent. Please name the entity.")
            bound[find(ep.args["ARG0"])] = focus[0]

    def term(value):
        if not value:
            raise Unsupported("A required grammatical argument is missing.")
        if value.startswith("@"):
            return value[1:]
        if not value.startswith(("x", "i", "u")):
            raise Unsupported("This event/scope argument cannot be used as an entity.")
        root = find(value)
        return bound.get(root, "?" + root)

    goals, assumptions = [], []
    for predicate, subject, obj, ep in raw_goals:
        negative = any(ep.label in s for s in negatives)
        goal = Goal(predicate, term(subject), term(obj), negative)
        target = assumptions if ep.label in assumption_scope else goals
        if goal not in target:
            target.append(goal)
    used_terms = {value for g in goals + assumptions for value in (g.subject, g.object)}
    if any(identity not in used_terms for identity in bound.values()):
        raise Unsupported("A named entity is disconnected from the supported interpretation.")
    if conditional and not assumptions:
        raise Unsupported("The conditional premise could not be represented without dropping its scope.")
    for scope_set in negatives:
        affected = {(p,term(s),term(o)) for p,s,o,ep in raw_goals if ep.label in scope_set}
        if len(affected) > 1:
            raise Unsupported("Negation over multiple propositions needs a richer scope representation.")
    if any(sum(ep.label in scope_set for scope_set in negatives)>1 for _,_,_,ep in raw_goals):
        raise Unsupported("Nested negation needs a richer scope representation.")
    temporal = "present"
    if any(variables.get(e.args.get("ARG0"), {}).get("TENSE") == "past" for e in eps):
        if not all(g.predicate in ("birthplace", "authored", "author", "type") for g in goals):
            raise Unsupported("A historical state needs an explicit time; current facts cannot answer it.")
        temporal = "record"
    selected = sorted({term(v) for v in wh if term(v).startswith("?")})
    # A wh identity can be unified with a grounded name; it is still an answer.
    if not selected and wh:
        selected = sorted({term(v) for v in wh})
    question_terms={x for g in goals for x in (g.subject,g.object)}
    if not conditional and any(v not in question_terms for v in selected):
        raise Unsupported("The question's requested value is disconnected from the supported relations.")
    selected = [v for v in selected if v in question_terms]
    act = "query" if is_question else "assert"
    if conditional:
        act = "hypothetical"
    if act == "assert" and any(v.startswith("?") for g in goals for v in (g.subject, g.object)):
        raise Unsupported("Assertions must identify their entities; generic rules require a declared rule module.")
    result = Meaning(act, goals, selected, labels, assumptions,
                     count=bool(counted), explain=why, temporal=temporal)
    result.semantics = [{"predicate": e.predicate, "label": e.label,
                         "args": dict(e.args), "carg": e.carg} for e in eps]
    return result
