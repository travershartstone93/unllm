"""MRS semantic roles -> typed component requirements.

Lexical concepts carry domain meaning, while argument/compound/conjunction
edges bind roles. New program names do not select code. Unconsumed content and
multiple compatible targets reject instead of being silently discarded.
"""
from fractions import Fraction
from math import lcm
from upgrade.model import Unsupported,Ambiguous
from upgrade.semantics import lemma
from .lexicon import DATA
from .spec import BehaviourSpec

CREATE={"write","create","generate","build","make"}
STRUCTURAL={"pron","pronoun_q","compound","mod","appos","poss","unknown","addressee",
            "relative_mod","_and_c","_but_c","implicit_conj","nominalization","generic_entity"}
FUNCTION_WORDS={"a","an","the","of","for","to","from","by","with","in","that","be","can","have","include","support","allow"}
ARTIFACTS={"app","application","program","script","function","calculator","tool","window","interface"}
QUALIFIERS={"basic","simple","standard","desktop","graphical","editable","arithmetic","significant","precision"}
INTERFACE={"button","control","input","area","field","display","digit","case"}
NOUNS={"text","document","editor","word","character","line","count","counter","number","value",
       "list","collection","task","item","shopping","timer","clock","elapsed_time","amount","bill","percentage","percent","rate"}
TEXT_OPS={"uppercase":"upper","upper":"upper","lowercase":"lower","lower":"lower","trim":"strip","strip":"strip",
          "reverse":"reverse_lines","sort":"sort_lines","unique":"unique_lines"}
MATH_OPS={"increment":("+","1"),"increase":("+","1"),"decrement":("-","1"),"decrease":("-","1"),
          "double":("*","2"),"triple":("*","3"),"multiply":("*",None),"reset":("reset","0")}
IO={"open":"load","load":"load","save":"save","new":"new","undo":"undo","redo":"redo","cut":"cut","copy":"copy",
    "paste":"paste","select_all":"select_all"}
SHORTCUTS={"new":"Ctrl+N","load":"Ctrl+O","save":"Ctrl+S","save_as":"Ctrl+Shift+S","undo":"Ctrl+Z",
           "redo":"Ctrl+Shift+Z","cut":"Ctrl+X","copy":"Ctrl+C","paste":"Ctrl+V","select_all":"Ctrl+A"}


class Graph:
    def __init__(self,eps):
        self.eps=eps;self.words={};self.children={};self.parents={};self.named={}
        for index,ep in enumerate(eps):
            args=ep["args"];var=args.get("ARG0");word=self.word(ep)
            if "_n_" in ep["predicate"] or ep["predicate"]=="named":
                self.words.setdefault(var,set()).add(word)
            if ep["predicate"]=="named":self.named[var]=str(ep.get("carg","")).casefold()
            if ep["predicate"]=="compound":
                self.edge(args.get("ARG1"),args.get("ARG2"))
            if ep["predicate"] in ("_and_c","implicit_conj","appos"):
                root=args.get("ARG0") if ep["predicate"]!="appos" else args.get("ARG1")
                for key in ("ARG1","ARG2"):
                    if args.get(key)!=root:self.edge(root,args.get(key))
        self.terms_cache={}

    def word(self,ep):
        return ep.get("lexical_concept") or (str(ep.get("carg","")).casefold() if ep["predicate"]=="named" else lemma(ep["predicate"]).split("/")[0])

    def edge(self,head,child):
        if not head or not child:return
        self.children.setdefault(head,set()).add(child);self.parents.setdefault(child,set()).add(head)

    def descendants(self,var):
        found=set();todo=[var]
        while todo:
            node=todo.pop()
            if node in found:continue
            found.add(node);todo.extend(self.children.get(node,()))
        return found

    def terms(self,var):
        if var not in self.terms_cache:
            self.terms_cache[var]=set().union(*(self.words.get(v,set()) for v in self.descendants(var)))
        return self.terms_cache[var]

    def units(self,var):return self.terms(var)&set(DATA["units"])

    def action_targets(self,ep):
        return set().union(*(self.terms(v) for k,v in ep["args"].items() if k in ("ARG1","ARG2","ARG3","ARG4")))


class Requirements:
    def __init__(self,graph):
        self.graph=graph;self.state={};self.views=[];self.actions=[];self.events=[]
        self.bindings=[];self.defaults=[];self.targets={}

    def field(self,name,kind,initial,**metadata):
        if name in self.state:raise Ambiguous("Two semantic roles need the same state name.")
        self.state[name]={"type":kind,"initial":initial,**metadata}

    def view(self,name,kind,**args):self.views.append({"id":name,"kind":kind,**args})
    def action(self,name,**args):
        if any(a["id"]==name for a in self.actions):return
        self.actions.append({"id":name,"label":name.replace("_"," ").title(),**args})

    def bind(self,operation,kinds,ep=None):
        candidates=[name for name,kind in self.targets.items() if kind in kinds]
        terms=self.graph.action_targets(ep) if ep else set()
        explicit=[name for name in candidates if name in terms or
                  (self.targets[name]=="records" and terms&{"list","collection","task","item"}) or
                  (self.targets[name]=="clock" and terms&{"timer","clock","elapsed_time"}) or
                  (name=="body" and terms&{"text","document","editor"}) or
                  (name=="value" and terms&{"counter","number","value"})]
        if explicit:candidates=explicit
        if len(candidates)!=1:raise Ambiguous("Specify the target for "+operation+"; compatible states: "+", ".join(candidates))
        self.bindings.append({"operation":operation,"state":candidates[0],"predicate":ep["predicate"] if ep else "default"})
        return candidates[0]

    def effect(self,name,effect,target,**params):
        args={"effect":effect,"state":target,**params}
        if effect in SHORTCUTS:args["shortcut"]=SHORTCUTS[effect]
        self.action(name,**args)

    def text(self,document=False,multiline=False):
        self.field("body","text","");self.targets["body"]="text"
        self.view("body_view","text_edit" if document or multiline else "line_edit",state="body")
        if document:
            self.defaults.append("Document editing supplies New/Open/Save, history and clipboard controls.")
            for op in ("new","load","save","save_as","undo","redo","cut","copy","paste","select_all"):self.effect(op,op,"body")

    def collection(self,fields):
        self.field("records","records",[],fields=fields);self.targets["records"]="records"
        self.field("selection","number",-1.0)
        for key,kind in fields.items():
            self.field("entry_"+key,kind,False if kind=="bool" else 0.0 if kind=="number" else "")
            if kind!="bool":self.view("entry_"+key+"_input","number_input" if kind=="number" else "line_edit",
                                      state="entry_"+key,label=key.title())
        text=next((key for key,kind in fields.items() if kind=="text"),None)
        if text is None:raise Unsupported("A collection needs a text display field.")
        check=next((key for key,kind in fields.items() if kind=="bool"),None)
        self.view("records_view","record_list",state="records",selection="selection",display=text,
                  **({"checked":check} if check else {}))
        self.view("records_count_view","label",value="records_count(records)",label="Items")

    def clock(self):
        self.field("elapsed","number",0.0);self.field("anchor","number",0.0);self.field("running","bool",False)
        self.targets["elapsed"]="clock";self.view("elapsed_view","label",value="elapsed",label="Seconds")
        self.effect("tick","sample_clock","elapsed",anchor="anchor",running="running",visible=False)
        self.events.append({"kind":"periodic","interval_ms":25,"action":"tick"})

    def scalar(self):
        self.field("value","number",0.0);self.targets["value"]="number"
        self.view("number_view","number_input",state="value");self.view("number_result","label",value="value",label="Value")

    def percentage(self,concept):
        definition=DATA["concepts"][concept]
        for name in ("amount","rate","part","total"):self.field(name,"number",0.0)
        self.view("amount_input","number_input",state="amount",label=definition["base_label"])
        self.view("rate_input","number_input",state="rate",label=concept.title()+" percentage")
        self.view("part_output","label",value="part",label=concept.title()+" amount")
        self.view("total_output","label",value="total",label="Total")
        sign="+" if definition["total_sign"]==1 else "-"
        self.action("calculate",ensure="100 * next.part == prev.amount * prev.rate and next.total == prev.amount "+sign+" next.part")
        self.bindings.append({"operation":"percentage_of","base":"amount","rate":"rate","output":"part","total_sign":definition["total_sign"]})

    def conversion(self,source,target):
        a,b=DATA["units"][source],DATA["units"][target]
        if a["dimension"]!=b["dimension"]:raise Unsupported("Conversion dimensions differ.")
        scale_a,scale_b=Fraction(a["scale"]),Fraction(b["scale"])
        offset=Fraction(a["offset"])-Fraction(b["offset"])
        denominator=lcm(scale_a.denominator,scale_b.denominator,offset.denominator)
        left=int(scale_b*denominator);right=int(scale_a*denominator);constant=int(offset*denominator)
        self.field("input_value","number",0.0);self.field("output_value","number",float(offset/scale_b))
        self.view("source_input","number_input",state="input_value",label=a["label"])
        self.view("converted_output","label",value="output_value",label=b["label"])
        self.action("convert",ensure=f"{left} * next.output_value == {right} * prev.input_value + ({constant})")
        self.bindings.append({"operation":"convert","source_unit":source,"target_unit":target,"dimension":a["dimension"]})

    def result(self):
        if not self.state:raise Unsupported("Describe inputs, state or required operations.")
        return BehaviourSpec.from_dict({"schema":2,"program":"application","title":"Application",
            "state":self.state,"views":self.views,"actions":self.actions,"events":self.events,
            "interpretation":{"method":"ERG semantic frames, typed role binding and explicit domain definitions",
                              "bindings":self.bindings,"defaults":self.defaults}})


def conversion_roles(graph,ep):
    args=ep["args"];linked={args.get("ARG0"),args.get("ARG1")}
    source=set();target=set()
    for other in graph.eps:
        word=lemma(other["predicate"])
        if word not in ("from","to") or other["args"].get("ARG1") not in linked:continue
        units=graph.units(other["args"].get("ARG2"))
        (source if word=="from" else target).update(units)
    if not source:source.update(graph.units(args.get("ARG2")))
    if len(source)!=1 or len(target)!=1:raise Ambiguous("Specify one source and one target unit for conversion.")
    return next(iter(source)),next(iter(target))


def operand(graph,ep,used,default):
    linked=set(ep["args"].values());found=[]
    aliases={}
    for item in graph.eps:
        if item["predicate"]=="appos":
            a,b=item["args"].get("ARG1"),item["args"].get("ARG2")
            aliases.setdefault(a,set()).add(b);aliases.setdefault(b,set()).add(a)
    for prep in graph.eps:
        if lemma(prep["predicate"])!="by" or prep["args"].get("ARG1") not in linked:continue
        targets={prep["args"].get("ARG2")}
        for var in tuple(targets):targets.update(aliases.get(var,()))
        for index,card in enumerate(graph.eps):
            if card["predicate"]=="card" and targets&set(card["args"].values()):
                try:number=float(card["carg"])
                except (TypeError,ValueError):raise Unsupported("Invalid numeric operand.")
                found.append((number,index))
    values={value for value,_ in found}
    if len(values)>1:raise Ambiguous("More than one amount is linked to the action.")
    used.update(index for _,index in found)
    if not values and default is None:raise Unsupported("The action needs an explicit numeric operand.")
    return repr(next(iter(values))) if values else default


def ground(eps):
    graph=Graph(eps)
    words={graph.word(e) for e in eps}
    percentages={word for word in words if DATA["concepts"].get(word,{}).get("type")=="percentage_relation"}
    conversions=[e for e in eps if lemma(e["predicate"])=="convert" and "_v_" in e["predicate"]]
    interactive=any(DATA["concepts"].get(word,{}).get("type")=="interactive_expression" for word in words)
    mathematical=bool(percentages or conversions or any("_v_" in e["predicate"] and graph.word(e) in MATH_OPS for e in eps))
    if interactive and not mathematical:
        other_goals=words&{"list","collection","timer","clock","elapsed_time","text","document","editor","counter","number"}
        if other_goals:
            raise Unsupported("Specify the arithmetic operation alongside the other behaviour goals; a default calculator cannot consume them.")
        from synthesis.apps.english import from_analysis
        return from_analysis(eps)
    if not words&CREATE:raise Unsupported("A construction request needs an explicit creation goal.")
    if any(e["predicate"]=="neg" for e in eps) or words&{"without","only","no"}:
        raise Unsupported("Scoped exclusions need an explicit supported behaviour contract.")
    known=set(DATA["concepts"])|set(DATA["units"])|ARTIFACTS|QUALIFIERS|INTERFACE|NOUNS|set(TEXT_OPS)|set(MATH_OPS)|set(IO)|{
        "start","stop","pause","append","add","insert","remove","delete","reset","select","all","duplicate","calculate","convert","operation","python","c++","cpp"}
    for ep in eps:
        p=ep["predicate"];word=graph.word(ep)
        if lemma(p)=="operation" and ep.get("lexical_kind")!="action":
            raise Unsupported("An operation label needs an explicit lexical meaning.")
        if p in STRUCTURAL or p.endswith("_q") or p=="card":continue
        if p=="named" and word in ("basic","python","c++","cpp"):continue
        if word in CREATE|FUNCTION_WORDS|known:continue
        raise Unsupported("No semantic frame is defined for "+p)
    req=Requirements(graph);used=set()
    if len(percentages)>1:raise Ambiguous("Specify how the percentage relations combine.")
    if percentages:req.percentage(next(iter(percentages)))
    if conversions:
        if len(conversions)>1:raise Ambiguous("Specify each conversion separately.")
        req.conversion(*conversion_roles(graph,conversions[0]))
    collection=bool(words&{"list","collection"})
    if collection:
        fields={"text":"text"}
        for word in words:fields.update(DATA["concepts"].get(word,{}).get("fields",{}))
        req.collection(fields)
    clock=bool(words&{"elapsed_time","timer","clock"})
    if clock:req.clock()
    document="editor" in words
    text=bool(words&{"text","document","editor","word","character","editable"}) or "line" in words and "count" in words
    if text:req.text(document=document,multiline=bool(words&{"area","document","editor"}))
    scalar=bool(words&{"counter","number","value"}) and not percentages and not conversions
    if scalar:req.scalar()
    for count in (e for e in eps if "_n_" in e["predicate"] and lemma(e["predicate"])=="count"):
        terms=graph.terms(count["args"].get("ARG0"))
        matches=[item for item in (("word","words","Words"),("character","chars","Characters"),("line","lines","Lines")) if item[0] in terms]
        if len(matches)==1:
            _,operator,label=matches[0];target=req.bind(operator,("text",))
            req.view(operator+"_view","label",value=operator+"("+target+")",label=label)
        elif collection and terms&{"item","list","collection","task"}:
            req.bind("records_count",("records",),count)
        else:raise Unsupported("The count has no uniquely bound quantity.")
    explicit_numeric=False;explicit_clock=set();explicit_collection=set()
    for ep in eps:
        p=ep["predicate"];word=graph.word(ep)
        action=ep.get("lexical_kind")=="action" or "_v_" in p
        if not action or word in CREATE|FUNCTION_WORDS or word=="convert":continue
        if word in TEXT_OPS or word in TEXT_OPS.values():
            operation=TEXT_OPS.get(word,word);target=req.bind(operation,("text",),ep)
            req.action(operation,assign={target:operation+"("+target+")"})
        elif word=="duplicate":
            raise Unsupported("Duplicate preservation/removal needs an explicit action.")
        elif word in IO:
            effect=IO[word];target=req.bind(effect,("text","records"),ep);req.effect(effect,effect,target)
        elif word in ("append","add","insert") and collection:
            target=req.bind("append",("records",),ep);fields=req.state[target]["fields"]
            values={key:("strip(entry_"+key+")" if kind=="text" else "entry_"+key) for key,kind in fields.items()}
            clear=["entry_"+k for k,t in fields.items() if t=="text"]
            req.effect("add","append_record",target,values=values,selection="selection",clear=clear,
                       requires=" and ".join("chars(strip("+n+")) > 0" for n in clear) or "True")
            explicit_collection.add("add")
        elif word in ("remove","delete") and collection:
            target=req.bind("remove",("records",),ep);req.effect("remove","remove_record",target,selection="selection")
            explicit_collection.add("remove")
        elif word in ("start","stop","pause","reset") and clock:
            target=req.bind(word,("clock","number") if word=="reset" and scalar else ("clock",),ep)
            if req.targets[target]=="number":
                req.action("reset",ensure="next."+target+" == 0");explicit_numeric=True;continue
            operation="stop" if word=="pause" else word
            req.effect(operation,operation+"_clock",target,anchor="anchor",running="running");explicit_clock.add(operation)
        elif word in MATH_OPS:
            target=req.bind(word,("number",),ep);operator,default=MATH_OPS[word]
            value=operand(graph,ep,used,default)
            name="increment" if word in ("increment","increase") else "decrement" if word in ("decrement","decrease") else word
            formula=value if operator=="reset" else "prev."+target+" "+operator+" "+value
            req.action(name,ensure="next."+target+" == "+formula);explicit_numeric=True
        elif word in ("calculate",):continue
        else:raise Unsupported("No action frame is defined for "+p)
    if scalar and not explicit_numeric:
        for name,formula in (("increment","prev.value + 1"),("decrement","prev.value - 1"),("reset","0")):
            req.action(name,ensure="next.value == "+formula)
        req.defaults.append("A numeric counter supplies increment/decrement/reset when no change action is specified.")
    if clock and not explicit_clock:
        for operation in ("start","stop","reset"):req.effect(operation,operation+"_clock","elapsed",anchor="anchor",running="running")
        req.defaults.append("Elapsed-time measurement supplies start/stop/reset controls.")
    if collection and not explicit_collection:
        target="records";fields=req.state[target]["fields"];clear=["entry_"+k for k,t in fields.items() if t=="text"]
        req.effect("add","append_record",target,values={k:("strip(entry_"+k+")" if t=="text" else "entry_"+k) for k,t in fields.items()},
                   selection="selection",clear=clear,requires=" and ".join("chars(strip("+n+")) > 0" for n in clear) or "True")
        req.effect("remove","remove_record",target,selection="selection")
        req.defaults.append("Editable collections supply insert/remove controls.")
    if any(e["predicate"]=="card" and i not in used for i,e in enumerate(eps)):
        raise Unsupported("A numeric requirement has no bound role.")
    return req.result()
