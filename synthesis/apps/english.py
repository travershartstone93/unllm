"""Calculator requirements from MRS concepts, arguments and negative scope."""
from upgrade.model import Unsupported,Ambiguous,digest
from upgrade.semantics import lemma
from .spec import ApplicationSpec,OPERATIONS

FEATURES={"memory":"memory","parenthesis":"parentheses","parentheses":"parentheses","bracket":"parentheses",
          "backspace":"backspace","keyboard":"keyboard",
          "add":"add","addition":"add","plus":"add","subtract":"subtract","subtraction":"subtract","minus":"subtract",
          "multiply":"multiply","multiplication":"multiply","times":"multiply","divide":"divide","division":"divide"}
STRUCTURAL={"named","proper_q","pron","pronoun_q","udef_q","def_implicit_q","def_explicit_q","generic_entity",
            "compound","mod","appos","poss","unknown","addressee","relative_mod","_and_c","_but_c","implicit_conj","neg"}
WORDS={"write","create","generate","make","build","program","script","function","app","application","calculator",
       "standard","basic","simple","desktop","graphical","interface","window","button","control","support","include",
       "enable","allow","with","without","except","only","no","not","use","have","and","a","an","the","that",
       "for","in","of","to","python","c++","cpp","number","decimal","negative","input","display","digit",
       "precision","significant","arithmetic",*FEATURES}


def from_analysis(eps):
    if not any(lemma(e["predicate"])=="calculator" for e in eps):raise Unsupported("No application family.")
    words={lemma(e["predicate"]) for e in eps}
    for ep in eps:
        pred=ep["predicate"]
        if pred in STRUCTURAL or pred.endswith("_q") or pred=="card":continue
        if lemma(pred) not in WORDS:raise Unsupported("Unsupported application requirement: "+pred)
    negative_vars={e["args"].get("ARG0") for e in eps if lemma(e["predicate"])=="no"}
    negative_labels={label for e in eps if e["predicate"]=="neg" for label in e.get("scope",[])}
    for ep in eps:
        if ep.get("label") in negative_labels:
            word=lemma(ep["predicate"])
            if word in ("write","create","make","build","generate"):
                raise Unsupported("A negated creation request does not authorize generating an application.")
            if word in ("support","include","enable","allow","have"):negative_vars.add(ep["args"].get("ARG2"))
    changed=True
    while changed:
        before=set(negative_vars)
        for ep in eps:
            if ep["predicate"]=="compound" and ep["args"].get("ARG1") in negative_vars:
                negative_vars.add(ep["args"].get("ARG2"))
            if ep["predicate"] in ("_and_c","implicit_conj") and ep["args"].get("ARG0") in negative_vars:
                negative_vars.update(v for k,v in ep["args"].items() if k!="ARG0")
        changed=before!=negative_vars
    for ep in eps:
        if lemma(ep["predicate"])=="no":
            targets={ep["args"].get("ARG0")}
            for related in eps:
                if related["predicate"]=="compound" and related["args"].get("ARG1") in targets:
                    targets.add(related["args"].get("ARG2"))
            if not any(e["args"].get("ARG0") in targets and lemma(e["predicate"]) in FEATURES for e in eps):
                raise Unsupported("The negated application requirement is not supported.")
    for ep in eps:
        if lemma(ep["predicate"]) in ("without","except"):
            target=ep["args"].get("ARG2")
            targets={target}
            for joined in eps:
                if joined["predicate"] in ("_and_c","implicit_conj") and joined["args"].get("ARG0") in targets:
                    targets.update(v for k,v in joined["args"].items() if k!="ARG0")
                if joined["predicate"]=="compound" and joined["args"].get("ARG1") in targets:
                    targets.add(joined["args"].get("ARG2"))
            if not any(e["args"].get("ARG0") in targets and lemma(e["predicate"]) in FEATURES for e in eps):
                raise Unsupported("The excluded application requirement has no supported scope.")
            negative_vars.update(targets)
    enabled=set();disabled=set()
    for ep in eps:
        feature=FEATURES.get(lemma(ep["predicate"]))
        if feature is None:continue
        negative=ep["args"].get("ARG0") in negative_vars or ep.get("label") in negative_labels
        (disabled if negative else enabled).add(feature)
    if any(e["predicate"]=="neg" for e in eps) and not disabled:
        raise Unsupported("The negative application requirement has no supported feature scope.")
    if enabled & disabled:raise Ambiguous("The application request both enables and disables the same feature.")
    only="only" in words
    operations=set(OPERATIONS)
    if only:
        operations=enabled & set(OPERATIONS)
        if not operations:raise Unsupported("Only needs an explicit list of supported arithmetic operations.")
    operations-=disabled
    data={"program":"calculator","operations":[op for op in OPERATIONS if op in operations]}
    for name in ("memory","parentheses","backspace","keyboard"):
        if name in enabled:data[name]=True
        if name in disabled:data[name]=False
    numbers=[]
    for ep in eps:
        if ep.get("carg") is not None:
            try:numbers.append(float(ep["carg"]))
            except ValueError:pass
    if numbers:
        if len(numbers)!=1 or not numbers[0].is_integer() or not words & {"precision","significant"}:
            raise Unsupported("A number in the application request has no supported requirement.")
        data["precision"]=int(numbers[0])
    return ApplicationSpec.from_dict(data)


def interpret_application(meaning):
    choices={};errors=[]
    for analysis in meaning.analyses or [meaning.semantics]:
        try:
            spec=from_analysis(analysis);choices.setdefault(digest(spec.data()),spec)
        except (Unsupported,Ambiguous) as error:errors.append(error)
    if not choices:
        if errors:raise errors[0]
        raise Unsupported("No supported application interpretation.")
    if len(choices)>1:raise Ambiguous("Different application requirements were found; use an explicit JSON specification.")
    return next(iter(choices.values()))
