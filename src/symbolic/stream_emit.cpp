#include "unllm/symbolic/emit.h"
#include <map>
namespace unllm::native {
std::string emit_expression(const Expr &e, const std::string &language) {
  e.type();
  bool py = language == "python";
  if (!py && language != "cpp")
    throw Unsupported("Unknown target language");
  if (e.op == "variable")
    return "x";
  if (e.op == "constant") {
    if (e.value.is_boolean())
      return e.value.flag() ? (py ? "True" : "true") : (py ? "False" : "false");
    return canonical(py ? e.value : Json::real(e.value.real()));
  }
  std::vector<std::string> args;
  for (auto &a : e.args)
    args.push_back(emit_expression(a, language));
  const std::map<std::string, std::string> binary{{"add", "+"},
                                                  {"subtract", "-"},
                                                  {"multiply", "*"},
                                                  {"gt", ">"},
                                                  {"ge", ">="},
                                                  {"lt", "<"},
                                                  {"le", "<="},
                                                  {"eq", "=="},
                                                  {"and", py ? "and" : "&&"},
                                                  {"or", py ? "or" : "||"}};
  if (binary.contains(e.op))
    return "(" + args[0] + " " + binary.at(e.op) + " " + args[1] + ")";
  if (e.op == "not" || e.op == "negate")
    return "(" +
           std::string(e.op == "negate" ? "-"
                       : py             ? "not "
                                        : "!") +
           args[0] + ")";
  if (e.op == "if")
    return py ? "(" + args[1] + " if " + args[0] + " else " + args[2] + ")"
              : "(" + args[0] + " ? " + args[1] + " : " + args[2] + ")";
  throw Unsupported("Unsupported emitted expression");
}
namespace {
struct Code {
  std::string text;
  int indent = 0;
  void line(const std::string &s = "") {
    text += std::string(indent * 4, ' ') + s + "\n";
  }
  void open(const std::string &s) {
    line(s);
    ++indent;
  }
  void close(const std::string &s = "") {
    --indent;
    if (!s.empty())
      line(s);
  }
};
std::string python(const Program &p, const std::filesystem::path &root) {
  Code c;
  c.text = read_file(root / "resources/targets/streams/common.py");
  c.line();
  c.open("def read_input(stream):");
  c.line(p.input == "records" ? "data = {}" : "data = []");
  c.open("for index, raw in enumerate(stream, 1):");
  c.open("if index > 100000:");
  c.line("raise ValueError(\"input exceeds 100000 rows\")");
  c.close();
  c.open("try:");
  c.line("line = checked_line(raw, index)");
  if (p.input != "lines") {
    c.open("if not line.strip():");
    c.line("continue");
    c.close();
  }
  if (p.input == "records") {
    c.line("group, separator, value = line.partition(\"\\t\")");
    c.line("group = group.strip()");
    c.open("if not separator or not group or \"\\t\" in value:");
    c.line("raise ValueError(\"expected key TAB number\")");
    c.close();
    c.line("number = parse_number(value)");
    c.line("data.setdefault(group, []).append(number)");
  } else
    c.line(p.input == "numbers" ? "data.append(parse_number(line))"
                                : "data.append(line)");
  c.close();
  c.open("except ValueError as error:");
  c.line(p.invalid == "skip" ? "pass"
                             : "raise ValueError(\"line \" + str(index) + \": "
                               "\" + str(error)) from error");
  c.close();
  c.close();
  c.line("return data");
  c.close();
  c.line();
  c.open("def transform(values):");
  for (auto &s : p.steps) {
    if (s.op == "filter")
      c.line("values = [x for x in values if " +
             emit_expression(*s.expr, "python") + "]");
    else if (s.op == "map")
      c.line("values = [checked_number(" + emit_expression(*s.expr, "python") +
             ") for x in values]");
    else if (s.op == "unique")
      c.line("values = list(dict.fromkeys(values))");
    else if (s.op == "sort")
      c.line("values = sorted(values, reverse=" +
             std::string(s.descending ? "True" : "False") + ")");
    else if (s.op == "reverse")
      c.line("values = list(reversed(values))");
    else if (s.op == "lowercase")
      c.line("values = [line.lower() for line in values]");
    else if (s.op == "strip")
      c.line("values = [line.strip() for line in values]");
    else if (s.op == "words")
      c.line("values = [word for line in values for word in line.split()]");
  }
  c.line("return values");
  c.close();
  c.line();
  c.open("def render_values(values):");
  if (p.output == "values")
    c.line(p.input == "lines" ? "return list(values)"
                              : "return [format_number(x) for x in values]");
  else if (p.output == "count")
    c.line("return [str(len(values))]");
  else if (p.output == "frequencies") {
    c.line("counts = {}");
    c.open("for value in values:");
    c.open("if \"\\t\" in value:");
    c.line("raise ValueError(\"frequency keys cannot contain tabs\")");
    c.close();
    c.line("counts[value] = counts.get(value, 0) + 1");
    c.close();
    c.line("return [\"value\\tcount\"] + [value + \"\\t\" + str(counts[value]) "
           "for value in sorted(counts)]");
  } else {
    c.open("if not values:");
    c.line(p.output == "sum"
               ? "return [\"0\"]"
               : "raise ValueError(\"no numeric values remain\")");
    c.close();
    if (p.output == "sum" || p.output == "mean" || p.output == "stats") {
      c.line("total = 0.0");
      c.open("for value in values:");
      c.line("total += value");
      c.close();
    }
    if (p.output == "min" || p.output == "stats")
      c.line("minimum = min(values)");
    if (p.output == "max" || p.output == "stats")
      c.line("maximum = max(values)");
    if (p.output == "mean" || p.output == "stats")
      c.line("average = checked_number(total / len(values))");
    if (p.output == "stats")
      c.line(
          "return [str(len(values)) + \"\\t\" + \"\\t\".join(format_number(x) "
          "for x in (total, minimum, maximum, average))]");
    else {
      const std::map<std::string, std::string> names{{"sum", "total"},
                                                     {"mean", "average"},
                                                     {"min", "minimum"},
                                                     {"max", "maximum"}};
      c.line("return [format_number(" + names.at(p.output) + ")]");
    }
  }
  c.close();
  c.line();
  c.open("def " + p.name + "(stream):");
  c.line("data = read_input(stream)");
  if (p.input == "records") {
    c.line(p.output == "stats"
               ? "output = [\"key\\tcount\\tsum\\tmin\\tmax\\tmean\"]"
               : "output = []");
    c.open("for group in sorted(data):");
    c.line("values = transform(data[group])");
    c.open("if not values:");
    c.line("continue");
    c.close();
    c.open("for row in render_values(values):");
    c.line("output.append(group + \"\\t\" + row)");
    c.close();
    c.close();
    c.line("return output");
  } else {
    c.line("values = transform(data)");
    c.line(p.output == "stats" ? "return [\"count\\tsum\\tmin\\tmax\\tmean\"] "
                                 "+ render_values(values)"
                               : "return render_values(values)");
  }
  c.close();
  c.line();
  c.open("def main():");
  c.open("try:");
  c.line("arguments = sys.argv[1:]");
  c.open("if len(arguments) > 1:");
  c.line("raise ValueError(\"usage: program [input-file]\")");
  c.close();
  c.open("if arguments:");
  c.open("with open(arguments[0], \"r\", encoding=\"ascii\") as stream:");
  c.line("output = " + p.name + "(stream)");
  c.close();
  c.close();
  c.open("else:");
  c.line("output = " + p.name + "(sys.stdin)");
  c.close();
  c.open("for line in output:");
  c.line("print(line)");
  c.close();
  c.line("return 0");
  c.close();
  c.open("except (OSError, ValueError, OverflowError) as error:");
  c.line("print(\"error: \" + str(error), file=sys.stderr)");
  c.line("return 2");
  c.close();
  c.close();
  c.line();
  c.open("if __name__ == \"__main__\":");
  c.line("raise SystemExit(main())");
  c.close();
  return c.text;
}
std::string cpp(const Program &p, const std::filesystem::path &root) {
  Code c;
  c.text = read_file(root / "resources/targets/streams/common.cpp.inc");
  bool numeric = p.input != "lines";
  std::string scalar = numeric ? "double" : "std::string";
  c.line("using Values = std::vector<" + scalar + ">;");
  c.line("using Data = " +
         std::string(p.input == "records" ? "std::map<std::string, Values>"
                                          : "Values") +
         ";");
  c.open("Data read_input(std::istream& stream) {");
  c.line("Data data;");
  c.line("std::string raw;");
  c.line("size_t index=0;");
  c.open("while(std::getline(stream,raw)) {");
  c.line("++index;");
  c.line("if(index>100000) throw std::runtime_error(\"input exceeds 100000 "
         "rows\");");
  c.open("try {");
  c.line("const auto line=checked_line(raw,index);");
  if (p.input == "records") {
    c.line("if(trim(line).empty()) continue;");
    c.line("const auto split=line.find('\\t');");
    c.line("if(split==std::string::npos || "
           "line.find('\\t',split+1)!=std::string::npos) throw "
           "std::runtime_error(\"expected key TAB number\");");
    c.line("const auto group=trim(line.substr(0,split));");
    c.line("if(group.empty()) throw std::runtime_error(\"empty group key\");");
    c.line("data[group].push_back(parse_number(line.substr(split+1)));");
  } else if (numeric) {
    c.line("if(trim(line).empty()) continue;");
    c.line("data.push_back(parse_number(line));");
  } else
    c.line("data.push_back(line);");
  c.close("} catch(const std::exception& error) {");
  ++c.indent;
  if (p.invalid != "skip")
    c.line("throw std::runtime_error(\"line \"+std::to_string(index)+\": "
           "\"+error.what());");
  c.close("}");
  c.close("}");
  c.line("if(stream.bad()) throw std::runtime_error(\"input read failed\");");
  c.line("return data;");
  c.close("}");
  c.open("Values transform(Values values) {");
  for (auto &s : p.steps) {
    if (s.op == "filter")
      c.line("{ Values next; for(const auto& x:values) if(" +
             emit_expression(*s.expr, "cpp") +
             ") next.push_back(x); values=std::move(next); }");
    else if (s.op == "map")
      c.line("for(auto& x:values) x=checked_number(" +
             emit_expression(*s.expr, "cpp") + ");");
    else if (s.op == "unique")
      c.line("{ std::set<" + scalar +
             "> seen; Values next; for(const auto& x:values) "
             "if(seen.insert(x).second) next.push_back(x); "
             "values=std::move(next); }");
    else if (s.op == "sort") {
      c.line("std::sort(values.begin(),values.end());");
      if (s.descending)
        c.line("std::reverse(values.begin(),values.end());");
    } else if (s.op == "reverse")
      c.line("std::reverse(values.begin(),values.end());");
    else if (s.op == "strip")
      c.line("for(auto& x:values) x=trim(x);");
    else if (s.op == "lowercase")
      c.line("for(auto& x:values) for(auto& ch:x) if(ch>='A' && ch<='Z') "
             "ch=char(ch-'A'+'a');");
    else if (s.op == "words")
      c.line("{ Values next; for(const auto& line:values) { std::istringstream "
             "in(line); std::string word; while(in>>word) "
             "next.push_back(word); } values=std::move(next); }");
  }
  c.line("return values;");
  c.close("}");
  c.open("std::vector<std::string> render_values(const Values& values) {");
  if (p.output == "values") {
    c.line("std::vector<std::string> out;");
    c.line("for(const auto& x:values) out.push_back(" +
           std::string(numeric ? "format_number(x)" : "x") + ");");
    c.line("return out;");
  } else if (p.output == "count")
    c.line("return {std::to_string(values.size())};");
  else if (p.output == "frequencies") {
    c.line("std::map<std::string,size_t> counts;");
    c.line("for(const auto& x:values) { if(x.find('\\t')!=std::string::npos) "
           "throw std::runtime_error(\"frequency keys cannot contain tabs\"); "
           "++counts[x]; }");
    c.line("std::vector<std::string> out{\"value\\tcount\"};");
    c.line("for(const auto& [value,count]:counts) "
           "out.push_back(value+\"\\t\"+std::to_string(count));");
    c.line("return out;");
  } else {
    c.line(p.output == "sum"
               ? "if(values.empty()) return {\"0\"};"
               : "if(values.empty()) throw std::runtime_error(\"no numeric "
                 "values remain\");");
    if (p.output == "sum" || p.output == "mean" || p.output == "stats") {
      c.line("double total=0;");
      c.line("for(double x:values) total+=x;");
    }
    if (p.output == "min" || p.output == "stats")
      c.line("const double "
             "minimum=*std::min_element(values.begin(),values.end());");
    if (p.output == "max" || p.output == "stats")
      c.line("const double "
             "maximum=*std::max_element(values.begin(),values.end());");
    if (p.output == "mean" || p.output == "stats")
      c.line("const double average=checked_number(total/values.size());");
    const std::map<std::string, std::string> outputs{
        {"sum", "format_number(total)"},
        {"mean", "format_number(average)"},
        {"min", "format_number(minimum)"},
        {"max", "format_number(maximum)"},
        {"stats", "std::to_string(values.size())+\"\\t\"+format_number(total)+"
                  "\"\\t\"+format_number(minimum)+\"\\t\"+format_number("
                  "maximum)+\"\\t\"+format_number(average)"}};
    c.line("return {" + outputs.at(p.output) + "};");
  }
  c.close("}");
  c.open("std::vector<std::string> " + p.name + "(std::istream& stream) {");
  c.line("auto data=read_input(stream);");
  if (p.input == "records") {
    c.line("std::vector<std::string> out" +
           std::string(p.output == "stats"
                           ? "{\"key\\tcount\\tsum\\tmin\\tmax\\tmean\"}"
                           : "{}") +
           ";");
    c.open("for(const auto& [group,items]:data) {");
    c.line("const auto values=transform(items);");
    c.line("if(values.empty()) continue;");
    c.line("for(const auto& row:render_values(values)) "
           "out.push_back(group+\"\\t\"+row);");
    c.close("}");
    c.line("return out;");
  } else {
    c.line("auto values=transform(std::move(data));");
    c.line("auto out=render_values(values);");
    if (p.output == "stats")
      c.line("out.insert(out.begin(),\"count\\tsum\\tmin\\tmax\\tmean\");");
    c.line("return out;");
  }
  c.close("}");
  c.open("int main(int argc,char** argv) {");
  c.open("try {");
  c.line(
      "if(argc>2) throw std::runtime_error(\"usage: program [input-file]\");");
  c.line("std::ifstream file;");
  c.line("std::istream* stream=&std::cin;");
  c.open("if(argc==2) {");
  c.line("file.open(argv[1]);");
  c.line("if(!file) throw std::runtime_error(\"cannot open input file\");");
  c.line("stream=&file;");
  c.close("}");
  c.line("const auto output=" + p.name + "(*stream);");
  c.line("for(const auto& row:output) std::cout<<row<<'\\n';");
  c.line("return 0;");
  c.close("} catch(const std::exception& error) {");
  ++c.indent;
  c.line("std::cerr<<\"error: \"<<error.what()<<'\\n';");
  c.line("return 2;");
  c.close("}");
  c.close("}");
  return c.text;
}
} // namespace
std::string emit_stream(const Program &program, const std::string &language,
                        const std::filesystem::path &resources) {
  auto checked = Program::from(program.data());
  if (language == "python")
    return python(checked, resources);
  if (language == "cpp")
    return cpp(checked, resources);
  throw Unsupported("Target language must be python or cpp");
}
} // namespace unllm::native
