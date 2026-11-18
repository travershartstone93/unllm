"""A typed interpretation of ERG program-request semantics."""
from upgrade.model import Unsupported, Ambiguous, digest
from upgrade.semantics import lemma
from .spec import Program

STRUCTURAL={"named","pron","pronoun_q","proper_q","udef_q","def_explicit_q","def_implicit_q",
            "generic_entity","relative_mod","compound","mod","appos","poss","unknown","addressee",
            "_and_c","implicit_conj","subord","card","numbered_hour","basic_numbered_hour"}
WORDS={"write","create","generate","make","program","script","function","code","utility",
       "python","c++","cpp","a","an","the","each","all","every","that","read","input",
       "print","output","return","result","value","number","line","text","word","record",
       "key","group","sum","total","average","mean","minimum","maximum","min","max","count",
       "statistic","frequency","occurrence","compute","calculate","report","sort","ascending",
       "descending","reverse","unique","duplicate","remove","deduplicate","keep","filter",
       "positive","negative","great","large","small","less","more","than","double","triple",
       "multiply","add","subtract","lowercase","lower","case","convert","strip","trim",
       "whitespace","space","invalid","skip","ignore","empty","numeric","by","for","of","to",
       "from","in","on","with","without","order","per"}


def from_analysis(eps):
    words={lemma(e["predicate"]) for e in eps}
    for ep in eps:
        p=ep["predicate"]
        word=lemma(p)
        if p in STRUCTURAL or p.endswith("_q") or p in ("more_comp","less_comp"):
            continue
        if word not in WORDS and not word.startswith("lowercase/"):
            raise Unsupported("Unsupported programming concept: "+p)
    if any(w in words for w in ("record","group","key")):
        kind="records"
    elif any(w in words for w in ("line","text","word")):
        kind="lines"
    elif any(w in words for w in ("number","value","numeric")):
        kind="numbers"
    else:
        raise Unsupported("Specify whether the input contains numbers, text lines or key/value records.")
    nouns={e["args"].get("ARG0"):lemma(e["predicate"]) for e in eps if "_n_" in e["predicate"]}
    literals={}
    for e in eps:
        if e.get("carg") is not None:
            try:literals[e["args"].get("ARG0")]=float(e["carg"])
            except (ValueError,TypeError):pass
    output_words={"sum":"sum","total":"sum","average":"mean","mean":"mean","minimum":"min",
                  "maximum":"max","min":"min","max":"max","count":"count"}
    outputs={value for word,value in output_words.items() if word in words}
    if "statistic" in words or {"count","min","max","mean"}.issubset(outputs):
        output="stats"
    elif "frequency" in words or "occurrence" in words:
        output="frequencies"
    elif len(outputs)==1:
        output=next(iter(outputs))
    elif outputs:
        raise Ambiguous("Specify one aggregate, or request full statistics.")
    else:
        output="values"
    steps=[]
    events=[]
    used_literals=set()
    for ep in eps:
        word=lemma(ep["predicate"])
        position=(ep.get("span") or [0])[0]
        if word in ("sort","reverse","double","triple","multiply","add","subtract","strip","trim") and "_v_" not in ep["predicate"]:
            raise Unsupported("This operation needs an explicit action or a typed JSON operation: "+word)
        if word in ("sort","reverse","double","triple","multiply","add","subtract","strip","trim") and "_v_" in ep["predicate"]:
            events.append((position,word,ep))
        if word in ("unique","deduplicate"):
            events.append((position,"unique",ep))
        if word=="duplicate":
            if not any(lemma(e["predicate"])=="remove" and e["args"].get("ARG2")==ep["args"].get("ARG0") for e in eps):
                raise Unsupported("Specify removal of duplicates; duplication and preservation are different operations.")
            events.append((position,"unique",ep))
        if word in ("lowercase","lower") or word.startswith("lowercase/"):
            events.append((position,"lowercase",ep))
        if word in ("positive","negative"):
            if nouns.get(ep["args"].get("ARG1")) not in ("number","value","record"):
                raise Unsupported("A numeric condition must modify an input value.")
            events.append((position,"filter",{"expr":"x > 0" if word=="positive" else "x < 0"}))
        if ep["predicate"] in ("more_comp","less_comp"):
            value=literals.get(ep["args"].get("ARG2"))
            adjective=next((e for e in eps if e["args"].get("ARG0")==ep["args"].get("ARG1")),None)
            if value is not None and adjective:
                adj=lemma(adjective["predicate"])
                if adj in ("great","large","more"):operator=">"
                elif adj in ("small","less"):operator="<"
                else:raise Unsupported("Unsupported numeric comparison.")
                events.append((position,"filter",{"expr":"x "+operator+" "+repr(value)}))
                used_literals.add(ep["args"]["ARG2"])
            else:
                raise Unsupported("A numeric comparison needs one explicit numeric boundary.")
    seen=set()
    for position,op,ep in sorted(events,key=lambda x:(x[0],x[1])):
        if op in ("unique","sort","reverse","lowercase","strip","trim"):
            step={"op":"strip" if op in ("strip","trim") else op}
            if op=="sort":step["descending"]="descending" in words
        elif op=="filter":step={"op":"filter","expr":ep["expr"]}
        elif op in ("double","triple"):
            step={"op":"map","expr":"x * "+("2" if op=="double" else "3")}
        else:
            # A multiplier/addend is a grammatical argument of the action or
            # of its linked preposition, not a number found anywhere in text.
            args=ep["args"]
            if op=="subtract":
                raise Unsupported("Specify subtraction with an explicit JSON expression so the operand order is unambiguous.")
            values=[literals[v] for k,v in args.items() if k!="ARG0" and v in literals]
            for other in eps:
                if other["args"].get("ARG1")==args.get("ARG0"):
                    values += [literals[v] for k,v in other["args"].items() if k!="ARG0" and v in literals]
                    used_literals.update(v for k,v in other["args"].items() if k!="ARG0" and v in literals)
            used_literals.update(v for k,v in args.items() if k!="ARG0" and v in literals)
            if len(set(values))!=1:raise Unsupported("The arithmetic operation needs one explicit numeric argument.")
            operator={"multiply":"*","add":"+","subtract":"-"}[op]
            step={"op":"map","expr":"x "+operator+" "+repr(values[0])}
        signature=digest([position,step])
        if signature not in seen:
            steps.append(step);seen.add(signature)
    if set(literals)-used_literals:
        raise Unsupported("A number in the request has no supported role in the program specification.")
    if "ascending" in words and "descending" in words:
        raise Ambiguous("Specify each sort direction in a JSON operation list.")
    if words & {"keep","filter"} and not any(step["op"]=="filter" for step in steps):
        raise Unsupported("Filtering requires an explicit supported condition.")
    if "remove" in words and not words & {"duplicate","whitespace","space"}:
        raise Unsupported("Specify the removal condition in a JSON operation list.")
    if kind=="lines" and "empty" in words:
        raise Unsupported("Removing empty text lines needs a supported text predicate; it is not silently inferred.")
    if kind=="lines" and "word" in words:
        steps.insert(0,{"op":"words"})
    invalid="skip" if "invalid" in words and ("skip" in words or "ignore" in words) else "error"
    result=Program.from_dict({"input":kind,"steps":steps,"output":output,"invalid":invalid,
        "assumptions":["Input is printable ASCII and tabs; numbers are finite decimals within +/-1e15.",
                       "Records use key TAB number. Results are written to standard output.",
                       "A single optional filename may replace standard input."]})
    return result


def interpret(meaning):
    from .behaviour import english as behaviours
    if behaviours.accepts(meaning):
        return behaviours.interpret(meaning)
    programs={}
    errors=[]
    for analysis in meaning.analyses or [meaning.semantics]:
        try:
            program=from_analysis(analysis)
            programs.setdefault(digest(program.data()),program)
        except (Unsupported,Ambiguous) as exc:errors.append(str(exc))
    if not programs:raise Unsupported(errors[0] if errors else "The programming request needs a typed specification.")
    if len(programs)>1:raise Ambiguous("The request has different supported program interpretations. Specify the input and operation order.")
    return next(iter(programs.values()))
