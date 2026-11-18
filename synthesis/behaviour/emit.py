"""Structural emission from the same handler graph for every application."""
from pathlib import Path
import ast
import json
from .expressions import NUMBER,TEXT,BOOL,RECORDS,CALLS,parse

ROOT=Path(__file__).parent
TYPES={NUMBER:"double",TEXT:"std::string",BOOL:"bool",RECORDS:"QJsonArray"}
SYMBOLS={"add":"+","subtract":"-","multiply":"*","divide":"/","eq":"==","ne":"!=",
         "lt":"<","le":"<=","gt":">","ge":">=","and":"&&","or":"||"}


def literal(value,language):
    if language=="python":return repr(value)
    if isinstance(value,(dict,list)):
        return ("parse_object" if isinstance(value,dict) else "parse_array")+"("+json.dumps(json.dumps(value,ensure_ascii=False),ensure_ascii=False)+")"
    if type(value) is bool:return "true" if value else "false"
    return json.dumps(value,ensure_ascii=False) if isinstance(value,str) else repr(float(value))


def expression(value,fields,language):
    q=lambda v:literal(v,language)
    prefix="self.state_" if language=="python" else "state_"
    if value.kind=="records":return ("[]" if language=="python" else "QJsonArray{}"),RECORDS
    if value.kind=="literal":
        kind=BOOL if type(value.value) is bool else TEXT if isinstance(value.value,str) else NUMBER
        result=q(value.value)
        if language=="cpp" and kind==TEXT:result="std::string("+result+")"
        return result,kind
    if value.kind=="state":return prefix+value.name,fields[value.name]["type"]
    values=[expression(a,fields,language) for a in value.args];args=[v for v,_ in values]
    if value.kind=="primitive":
        name=value.name if value.name in ("number_text","records_count","count_true") else "text_"+value.name
        return name+"("+", ".join(args)+")",CALLS[value.name][1]
    if value.kind=="unary":
        return ("("+("not " if language=="python" else "!")+args[0]+")",BOOL) if value.name=="not" else (
            ("(-"+args[0]+")" if value.name=="neg" else args[0]),NUMBER)
    if value.kind=="conditional":
        result=("("+args[1]+" if "+args[0]+" else "+args[2]+")" if language=="python" else
                "("+args[0]+" ? "+args[1]+" : "+args[2]+")")
        return result,values[1][1]
    op=value.name
    if op=="divide":return "checked_divide("+", ".join(args)+")",NUMBER
    symbol=("and" if op=="and" else "or") if language=="python" and op in ("and","or") else SYMBOLS[op]
    result="("+args[0]+" "+symbol+" "+args[1]+")"
    kind=BOOL if op in ("eq","ne","lt","le","gt","ge","and","or") else values[0][1]
    if kind==NUMBER:result="checked_number("+result+")"
    return result,kind


def effect_lines(action,fields,language):
    target=literal(action["target"],language);py=language=="python"
    lines=[]
    for step in action["steps"]:
        obj="self." if py else ""
        get="self.encode_state("+target+")" if py else "encode_state("+target+")"
        if step=="confirm_discard":lines.append("if not self.confirm_discard("+target+"): return" if py else "if (!confirm_discard("+target+")) return;")
        elif step.startswith("choose_"):
            call=obj+step+"("+target+")"
            lines += ["path = "+call,"if not path: return"] if py else ["auto path = "+call+";","if (path.empty()) return;"]
        elif step=="read_utf8":lines.append("content = self.read_utf8(path)" if py else "auto content = read_utf8(path);")
        elif step=="encode_state":lines.append("content = "+get if py else "auto content = "+get+";")
        elif step=="empty_state":lines.append("content = self.empty_state("+target+")" if py else "auto content = empty_state("+target+");")
        elif step=="commit_state":lines.append("self.commit({"+target+": self.decode_state("+target+", content)})" if py else "put_encoded("+target+", content);")
        elif step=="remember_path":lines.append("self.paths["+target+"] = path" if py else "paths["+target+"] = path;")
        elif step=="clear_path":lines.append("self.paths["+target+"] = ''" if py else "paths["+target+"].clear();")
        elif step=="mark_saved":lines.append(("self.saved[" if py else "saved[")+target+"] = "+get+("" if py else ";"))
        elif step=="write_atomic_utf8":lines.append(obj+"write_atomic_utf8(path, content)"+("" if py else ";"))
        elif step.startswith("edit_"):lines.append(obj+"edit("+target+", "+literal(step[5:],language)+")"+("" if py else ";"))
        elif step in ("append_record","set_record","remove_record"):
            params=action["parameters"];q=lambda v:literal(v,language)
            values=[]
            for key,source in params.get("values",{}).items():
                expr,kind=expression(parse(source,fields)[0],fields,language)
                if not py and kind==TEXT:expr="qtext("+expr+")"
                values.append((q(key),expr))
            record=("{"+", ".join(k+": "+v for k,v in values)+"}") if py else (
                "QJsonObject{"+", ".join("{"+k+", "+v+"}" for k,v in values)+"}")
            selection=q(params.get("selection",""))
            clear=q(params.get("clear",[])) if py else "std::vector<std::string>{"+", ".join(q(n) for n in params.get("clear",[]))+"}"
            args=[target,record,selection,clear] if step=="append_record" else [target,selection,record] if step=="set_record" else [target,selection]
            lines.append(obj+step+"("+", ".join(args)+")"+("" if py else ";"))
        elif step in ("read_clock","anchor_clock","enable_clock","disable_clock","update_duration","zero_duration"):
            params=action["parameters"];prefix="self.state_" if py else "state_"
            elapsed=prefix+action["target"];anchor=prefix+params["anchor"];running=prefix+params["running"]
            if step=="read_clock":lines.append("now = checked_number(self.clock())" if py else "double now = checked_number(monotonic_now());")
            elif step=="anchor_clock":
                lines += ["if "+running+": return",anchor+" = checked_number(now - "+elapsed+")"] if py else [
                    "if("+running+") return;",anchor+" = checked_number(now - "+elapsed+");"]
            elif step=="enable_clock":lines.append(running+(" = True" if py else " = true;"))
            elif step=="disable_clock":lines.append(running+(" = False" if py else " = false;"))
            elif step=="zero_duration":lines.append(elapsed+(" = 0.0" if py else " = 0.0;"))
            else:lines.append("if "+running+": "+elapsed+" = checked_number(now - "+anchor+")" if py else
                              "if("+running+") "+elapsed+" = checked_number(now - "+anchor+");")
        else:raise ValueError("No lowering for effect instruction "+step)
    return lines


def python(spec,ir):
    q=lambda v:literal(v,"python")
    lines=[(ROOT/"python_runtime.py").read_text(),"","class ApplicationWindow(BehaviourWindow):",
           "    def __init__(self, dialogs=None, clock=None):","        super().__init__(dialogs, clock)"]
    for name,field in ir["state"].items():lines.append("        self.state_"+name+" = "+q(field["initial"]))
    lines.append("        self.setWindowTitle("+q(spec.value["title"]+(" [*]" if ir["persistent"] else ""))+")")
    for name,field in ir["state"].items():
        lines.append("        self.register("+q(name)+", "+q(field["type"])+", lambda: self.state_"+name+
                     ", lambda value: setattr(self, "+q("state_"+name)+", value), "+q(name in ir["persistent"])+", "+q(field.get("fields"))+")")
    lines.append("        row = 0")
    for view in ir["views"]:
        if view["kind"]=="label":
            value,_=ir["labels"][view["id"]];expr,_=expression(value,ir["state"],"python")
            lines.append("        row = self.add_output("+q(view["id"])+", lambda: "+expr+", "+q(view.get("label",""))+", row)")
        elif view["kind"]=="record_list":
            lines.append("        row = self.add_records("+", ".join(q(view[k]) for k in ("id","state","selection","display"))+
                         ", "+q(view.get("checked",""))+", "+q(view.get("label",""))+", row)")
        else:
            lines.append("        row = self.add_input("+", ".join(q(view[k]) for k in ("id","kind","state"))+
                         ", "+q(view.get("label",""))+", row)")
    visible=[a for a in spec.actions if a.get("visible",True)]
    for index,action in enumerate(visible):
        lines.append("        self.add_action("+q(action["id"])+", "+q(action.get("label",action["id"]))+
                     ", self.on_"+action["id"]+", "+q(action.get("shortcut",""))+", row + "+str(index//4)+", "+str(index%4)+")")
    lines.append("        self.finish(row + "+str((len(visible)+3)//4)+")")
    for event in ir["events"]:lines.append("        self.add_periodic("+str(event["interval_ms"])+", self.on_"+event["action"]+")")
    for action in ir["actions"]:
        lines+=["","    def on_"+action["id"]+"(self):"]
        if action["updates"]:
            entries=[q(name)+": "+expression(value,ir["state"],"python")[0] for name,value in action["updates"].items()]
            body=["self.commit({"+", ".join(entries)+"})"]
        else:body=effect_lines(action,ir["state"],"python")
        if action["guard"]:
            guard,_=expression(action["guard"],ir["state"],"python")
            body=["if not "+guard+": raise ValueError('Check the inputs before continuing')",*body]
        lines+=["        "+line for line in body]
    lines+=["","def main():","    application = QApplication(sys.argv)","    application.setStyle('Fusion')",
            "    window = ApplicationWindow()","    window.show()","    return application.exec()",
            "","if __name__ == '__main__':","    raise SystemExit(main())"]
    module=ast.parse("\n".join(lines));compile(module,"<constructed program>","exec")
    return ast.unparse(module)+"\n"


def cpp(spec,ir):
    q=lambda v:literal(v,"cpp")
    lines=[(ROOT/"cpp_runtime.h").read_text(),"class ApplicationWindow:public BehaviourWindow {","public:"]
    for name,field in ir["state"].items():lines.append(TYPES[field["type"]]+" state_"+name+" = "+q(field["initial"])+";")
    lines+=["explicit ApplicationWindow(Dialogs service={},std::function<double()> clock={}):BehaviourWindow(std::move(service),std::move(clock)) {",
            "setWindowTitle(qtext("+q(spec.value["title"]+(" [*]" if ir["persistent"] else ""))+"));"]
    for name,field in ir["state"].items():
        if field["type"]==TEXT:
            lines.append("register_text("+q(name)+", [this]{return state_"+name+";}, [this](const std::string& v){state_"+
                         name+"=v;}, "+q(name in ir["persistent"])+");")
        elif field["type"]==NUMBER:
            lines.append("register_number("+q(name)+", [this]{return state_"+name+";}, [this](double v){state_"+name+"=v;});")
        elif field["type"]==RECORDS:
            lines.append("register_records("+q(name)+", [this]{return state_"+name+";}, [this](const QJsonArray& v){state_"+
                         name+"=v;}, "+q(field["fields"])+", "+q(name in ir["persistent"])+");")
    lines.append("int row=0;")
    for view in ir["views"]:
        if view["kind"]=="label":
            value,kind=ir["labels"][view["id"]];expr,_=expression(value,ir["state"],"cpp")
            if kind==NUMBER:expr="number_text("+expr+")"
            elif kind==BOOL:expr="std::string("+expr+' ? "true" : "false")'
            lines.append("row=add_output("+q(view["id"])+", [this]{return "+expr+";}, "+q(view.get("label",""))+", row);")
        elif view["kind"]=="record_list":
            lines.append("row=add_records("+", ".join(q(view[k]) for k in ("id","state","selection","display"))+", "+
                         q(view.get("checked",""))+", "+q(view.get("label",""))+", row);")
        else:lines.append("row=add_input("+", ".join(q(view[k]) for k in ("id","kind","state"))+", "+q(view.get("label",""))+", row);")
    visible=[a for a in spec.actions if a.get("visible",True)]
    for index,action in enumerate(visible):
        lines.append("add_action("+q(action["id"])+", "+q(action.get("label",action["id"]))+", [this]{on_"+action["id"]+
                     "();}, "+q(action.get("shortcut",""))+", row+"+str(index//4)+", "+str(index%4)+");")
    lines+=["finish(row+"+str((len(visible)+3)//4)+");"]
    for event in ir["events"]:lines.append("add_periodic("+str(event["interval_ms"])+", [this]{on_"+event["action"]+"();});")
    lines.append("}")
    for action in ir["actions"]:
        lines.append("void on_"+action["id"]+"() {")
        if action["guard"]:
            guard,_=expression(action["guard"],ir["state"],"cpp")
            lines.append("if(!("+guard+")) throw std::runtime_error(\"Check the inputs before continuing\");")
        if action["updates"]:
            for name,value in action["updates"].items():
                expr,kind=expression(value,ir["state"],"cpp")
                lines.append(TYPES[kind]+" proposed_"+name+" = "+expr+";")
                if kind==TEXT:lines.append("if(proposed_"+name+".size()>MAX_BYTES) throw std::runtime_error(\"Text exceeds 4 MiB\");")
                elif kind==NUMBER:lines.append("checked_number(proposed_"+name+");")
                elif kind==RECORDS:lines.append("validate_records("+q(name)+", proposed_"+name+");")
            for name in action["updates"]:lines.append("state_"+name+" = proposed_"+name+";")
        else:lines+=effect_lines(action,ir["state"],"cpp")
        lines.append("}")
    lines+=["};","#ifndef UNLLM_APP_LIBRARY","int main(int argc,char** argv) {",
            "QApplication application(argc,argv);application.setStyle(\"Fusion\");",
            "ApplicationWindow window;window.show();return application.exec();","}","#endif"]
    return "\n".join(lines)+"\n"
