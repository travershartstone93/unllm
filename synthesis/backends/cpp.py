"""C++20 lowering of the same typed operation plan."""
import json
from ..spec import Expr

COMMON=r'''
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

double checked_number(double value) {
    if (!std::isfinite(value) || std::abs(value) > 1e15)
        throw std::runtime_error("number outside the finite +/-1e15 domain");
    return value;
}
std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n\v\f");
    if (first == std::string::npos) return "";
    auto last = value.find_last_not_of(" \t\r\n\v\f");
    return value.substr(first, last-first+1);
}
double parse_number(const std::string& raw) {
    const std::string value = trim(raw);
    if (value.empty() || value.find_first_of("xXpP_") != std::string::npos)
        throw std::runtime_error("expected a decimal number");
    char* end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || end != value.c_str()+value.size())
        throw std::runtime_error("expected a decimal number");
    return checked_number(number);
}
std::string checked_line(std::string value, size_t index) {
    if (index > 100000) throw std::runtime_error("input exceeds 100000 rows");
    if (!value.empty() && value.back() == '\r') value.pop_back();
    if (value.size() > 16384) throw std::runtime_error("line too long");
    for (unsigned char ch : value)
        if (ch >= 127 || (ch < 32 && ch != '\t')) throw std::runtime_error("expected printable ASCII and tabs");
    return value;
}
std::string format_number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << checked_number(value);
    return out.str();
}
'''

MAIN=r'''
int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::runtime_error("usage: program [input-file]");
        std::ifstream file;
        std::istream* stream = &std::cin;
        if (argc == 2) {
            file.open(argv[1]);
            if (!file) throw std::runtime_error("cannot open input file");
            stream = &file;
        }
        const auto output = process_data(*stream);
        for (const auto& row : output) std::cout << row << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
'''


def expression(expr: Expr):
    if expr.op=="variable":return "x"
    if expr.op=="constant":
        if isinstance(expr.value,bool):return "true" if expr.value else "false"
        return repr(float(expr.value))
    args=[expression(a) for a in expr.args]
    binary={"add":"+","subtract":"-","multiply":"*","gt":">","ge":">=",
            "lt":"<","le":"<=","eq":"==","and":"&&","or":"||"}
    if expr.op in binary:return "("+args[0]+" "+binary[expr.op]+" "+args[1]+")"
    if expr.op in ("not","negate"):return "("+("!" if expr.op=="not" else "-")+args[0]+")"
    if expr.op=="if":return "("+args[0]+" ? "+args[1]+" : "+args[2]+")"
    raise ValueError("Unsupported C++ expression")


def emit(program):
    numeric=program.input!="lines"
    scalar="double" if numeric else "std::string"
    values="std::vector<"+scalar+">"
    data_type="std::map<std::string, Values>" if program.input=="records" else "Values"
    code=[COMMON,"using Values = "+values+";","using Data = "+data_type+";"]
    reader=[
        "Data read_input(std::istream& stream) {","Data data;","std::string raw;","size_t index=0;",
        "while (std::getline(stream,raw)) {","++index;",
        'if(index>100000) throw std::runtime_error("input exceeds 100000 rows");',"try {",
        "const auto line=checked_line(raw,index);"]
    if program.input=="records":
        reader.extend([
            'if (trim(line).empty()) continue;', "const auto split=line.find('\\t');",
            'if(split==std::string::npos || line.find(\'\\t\',split+1)!=std::string::npos) throw std::runtime_error("expected key TAB number");',
            "const auto group=trim(line.substr(0,split));",
            'if(group.empty()) throw std::runtime_error("empty group key");',
            "data[group].push_back(parse_number(line.substr(split+1)));"])
    elif numeric:
        reader.extend(["if(trim(line).empty()) continue;","data.push_back(parse_number(line));"])
    else:reader.append("data.push_back(line);")
    reader.extend(["} catch(const std::exception& error) {"])
    if program.invalid!="skip":
        reader.append('throw std::runtime_error("line "+std::to_string(index)+": "+error.what());')
    reader.extend(["}","}","if(stream.bad()) throw std::runtime_error(\"input read failed\");","return data;","}"])
    code.append("\n".join(reader))
    transform=["Values transform(Values values) {"]
    for step in program.steps:
        if step.op=="filter":
            transform.append("{ Values next; for(const auto& x:values) if("+expression(step.expr)+") next.push_back(x); values=std::move(next); }")
        elif step.op=="map":
            transform.append("for(auto& x:values) x=checked_number("+expression(step.expr)+");")
        elif step.op=="unique":
            transform.append("{ std::set<"+scalar+"> seen; Values next; for(const auto& x:values) if(seen.insert(x).second) next.push_back(x); values=std::move(next); }")
        elif step.op=="sort":
            transform.append("std::sort(values.begin(),values.end());")
            if step.descending:transform.append("std::reverse(values.begin(),values.end());")
        elif step.op=="reverse":transform.append("std::reverse(values.begin(),values.end());")
        elif step.op=="strip":transform.append("for(auto& x:values) x=trim(x);")
        elif step.op=="lowercase":transform.append("for(auto& x:values) for(auto& ch:x) if(ch>='A' && ch<='Z') ch=char(ch-'A'+'a');")
        elif step.op=="words":transform.append("{ Values next; for(const auto& line:values) { std::istringstream in(line); std::string word; while(in>>word) next.push_back(word); } values=std::move(next); }")
    transform.extend(["return values;","}"]);code.append("\n".join(transform))
    render=["std::vector<std::string> render_values(const Values& values) {"]
    if program.output=="values":
        render+=["std::vector<std::string> out;","for(const auto& x:values) out.push_back("+("format_number(x)" if numeric else "x")+");","return out;"]
    elif program.output=="count":render+=["return {std::to_string(values.size())};"]
    elif program.output=="frequencies":
        render += ['std::map<std::string,size_t> counts;',
                   'for(const auto& x:values) { if(x.find(\'\\t\')!=std::string::npos) throw std::runtime_error("frequency keys cannot contain tabs"); ++counts[x]; }',
                   'std::vector<std::string> out{"value\\tcount"};',
                   'for(const auto& [value,count]:counts) out.push_back(value+"\\t"+std::to_string(count));',
                   'return out;']
    else:
        if program.output=="sum":render.append('if(values.empty()) return {"0"};')
        else:render.append('if(values.empty()) throw std::runtime_error("no numeric values remain");')
        if program.output in ("sum","mean","stats"):
            render+=["double total=0;","for(double x:values) total+=x;"]
        if program.output in ("min","stats"):
            render.append("const double minimum=*std::min_element(values.begin(),values.end());")
        if program.output in ("max","stats"):
            render.append("const double maximum=*std::max_element(values.begin(),values.end());")
        if program.output in ("mean","stats"):
            render.append("const double average=checked_number(total/values.size());")
        outputs={"sum":"format_number(total)","mean":"format_number(average)","min":"format_number(minimum)",
                 "max":"format_number(maximum)",
                 "stats":'std::to_string(values.size())+"\\t"+format_number(total)+"\\t"+format_number(minimum)+"\\t"+format_number(maximum)+"\\t"+format_number(average)'}
        render.append("return {"+outputs[program.output]+"};")
    render.append("}");code.append("\n".join(render))
    process=["std::vector<std::string> "+program.name+"(std::istream& stream) {","auto data=read_input(stream);"]
    if program.input=="records":
        initial='{"key\\tcount\\tsum\\tmin\\tmax\\tmean"}' if program.output=="stats" else "{}"
        process+=["std::vector<std::string> out"+initial+";","for(const auto& [group,items]:data) {",
            "const auto values=transform(items);","if(values.empty()) continue;",
            'for(const auto& row:render_values(values)) out.push_back(group+"\\t"+row);',"}","return out;"]
    else:
        process+=["auto values=transform(std::move(data));","auto out=render_values(values);"]
        if program.output=="stats":process.append('out.insert(out.begin(),"count\\tsum\\tmin\\tmax\\tmean");')
        process.append("return out;")
    process.append("}");code.append("\n".join(process))
    # Only the generated function identifier is selected here; its spelling has
    # already passed identifier validation in the typed specification.
    main=MAIN.split("process_data")
    code.append(program.name.join(main))
    return "\n".join(code)+"\n"
