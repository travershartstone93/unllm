#include "unllm/symbolic/program.h"
#include "unllm/symbolic/proof.h"
#include <set>
namespace unllm::native {
Json Step::data() const {
  auto out = Json::object();
  out.set("op", Json::string(op));
  out.set("expr", expr ? expr->data() : Json{});
  out.set("descending", Json::boolean(descending));
  return out;
}
Program Program::from(const Json &data) {
  fields_only(data, {"schema", "input", "steps", "output", "invalid", "name",
                     "assumptions"});
  if (data.contains("schema") &&
      (!data["schema"].is_integer() || data["schema"].integer() != 1))
    throw Unsupported("Unsupported program specification schema.");
  Program p;
  auto input = data["input"];
  if (input.is_object()) {
    fields_only(input, {"kind"});
    input = input["kind"];
  }
  if (!input.is_string() ||
      !std::set<std::string>{"numbers", "lines", "records"}.contains(
          input.text()))
    throw Unsupported("Input must be numbers, lines or records.");
  p.input = input.text();
  if (data.contains("output"))
    p.output = required_text(data, "output");
  if (!std::set<std::string>{"values", "sum", "mean", "min", "max", "count",
                             "stats", "frequencies"}
           .contains(p.output))
    throw Unsupported("Unsupported output contract.");
  if (data.contains("invalid"))
    p.invalid = required_text(data, "invalid");
  if (p.invalid != "error" && p.invalid != "skip")
    throw Unsupported("Invalid-record handling must be error or skip.");
  auto steps = data.contains("steps") ? data["steps"] : Json::array();
  if (!steps.is_array() || steps.size() > 12)
    throw Unsupported("The program operation budget is 12.");
  for (auto item : steps.items()) {
    fields_only(item, {"op", "expr", "descending"});
    Step s;
    s.op = required_text(item, "op");
    if (!std::set<std::string>{"filter", "map", "unique", "sort", "reverse",
                               "lowercase", "strip", "words"}
             .contains(s.op))
      throw Unsupported("Unsupported operation: " + s.op);
    if (!item["expr"].null())
      s.expr = expression(item["expr"]);
    bool scalar = s.op == "map" || s.op == "filter";
    if (scalar != s.expr.has_value())
      throw Unsupported(scalar ? "Filter and map require expressions."
                               : "Only filter and map take expressions.");
    if (s.expr && s.expr->type() !=
                      (s.op == "filter" ? ExprType::Boolean : ExprType::Number))
      throw Unsupported(
          "Filters need Boolean expressions; maps need numeric expressions.");
    if (item.contains("descending")) {
      if (!item["descending"].is_boolean())
        throw Unsupported("Sort direction must be Boolean.");
      s.descending = item["descending"].flag();
    }
    if (s.descending && s.op != "sort")
      throw Unsupported("Only sorting has a direction.");
    if (scalar && p.input == "lines")
      throw Unsupported("Numeric expressions need numeric input.");
    if ((s.op == "lowercase" || s.op == "strip" || s.op == "words") &&
        p.input != "lines")
      throw Unsupported("Text operations need text input.");
    p.steps.push_back(s);
  }
  if (p.input == "lines" && p.output != "values" && p.output != "count" &&
      p.output != "frequencies")
    throw Unsupported("Text output supports values, count and frequencies.");
  if (p.input != "lines" && p.output == "frequencies")
    throw Unsupported("Frequency output currently needs text input.");
  if (data.contains("name"))
    p.name = required_text(data, "name");
  static const std::set<std::string> keywords{
      "False",  "None",   "True",    "and",      "as",       "assert", "async",
      "await",  "break",  "class",   "continue", "def",      "del",    "elif",
      "else",   "except", "finally", "for",      "from",     "global", "if",
      "import", "in",     "is",      "lambda",   "nonlocal", "not",    "or",
      "pass",   "raise",  "return",  "try",      "while",    "with",   "yield"};
  if (!identifier(p.name) || p.name.starts_with('_') ||
      keywords.contains(p.name))
    throw Unsupported("The function name must be a public identifier.");
  auto assumptions =
      data.contains("assumptions") ? data["assumptions"] : Json::array();
  if (!assumptions.is_array())
    throw Unsupported("Assumptions must be a text array");
  for (auto a : assumptions.items()) {
    if (!a.is_string())
      throw Unsupported("Assumptions must be text");
    p.assumptions.push_back(a.text());
  }
  return p;
}
Json Program::data() const {
  auto out = Json::object(), operations = Json::array(), notes = Json::array();
  for (auto &s : steps)
    operations.push(s.data());
  for (auto &a : assumptions)
    notes.push(Json::string(a));
  out.set("schema", Json::number(1));
  out.set("input", Json::string(input));
  out.set("steps", operations);
  out.set("output", Json::string(output));
  out.set("invalid", Json::string(invalid));
  out.set("name", Json::string(name));
  out.set("assumptions", notes);
  return out;
}
Catalog stream_catalog() {
  Catalog catalog;
  auto add = [&](const std::string &op, const std::string &input,
                 const std::string &output) {
    auto id = "component:" + op + ":" + input;
    for (auto [predicate, object] :
         std::vector<std::pair<std::string, std::string>>{
             {"implements", op}, {"accepts", input}, {"produces", output}}) {
      auto identity = Json::array();
      for (auto s : {id, predicate, object})
        identity.push(Json::string(s));
      Claim c;
      c.id = "contract:" + digest(identity);
      c.predicate = predicate;
      c.subject = id;
      c.object = object;
      c.source = "component-catalog-v1";
      c.origin = "metadata";
      catalog.facts.push(c.data());
    }
  };
  for (auto kind : {"numbers", "lines", "groups"}) {
    for (auto op : {"unique", "sort", "reverse"})
      add(op, kind, kind);
    for (auto op : {"values", "count"})
      add(op, kind, "report");
  }
  for (auto kind : {"numbers", "groups"}) {
    for (auto op : {"filter", "map"})
      add(op, kind, kind);
    for (auto op : {"sum", "mean", "min", "max", "stats"})
      add(op, kind, "report");
  }
  for (auto op : {"lowercase", "strip", "words"})
    add(op, "lines", "lines");
  add("frequencies", "lines", "report");
  for (std::string kind : {"numbers", "lines", "records"})
    add("read_" + kind, "stream", kind == "records" ? "groups" : kind);
  for (auto name : {"implements", "accepts", "produces", "compatible"}) {
    auto r = Json::object();
    r.set("name", Json::string(name));
    catalog.relations.push(relation(r));
  }
  return catalog;
}
Json plan(const Program &program, NativeEngine &native, Deadline deadline) {
  auto catalog = stream_catalog();
  std::vector<std::string> operations{"read_" + program.input};
  for (auto &s : program.steps)
    operations.push_back(s.op);
  operations.push_back(program.output);
  std::vector<Goal> goals;
  for (size_t i = 0; i < operations.size(); ++i) {
    auto component = "?component" + std::to_string(i),
         before = i == 0 ? "stream" : "?type" + std::to_string(i),
         after = i + 1 == operations.size() ? "report"
                                            : "?type" + std::to_string(i + 1);
    goals.push_back({"implements", component, operations[i]});
    goals.push_back({"accepts", component, before});
    goals.push_back({"produces", component, after});
    if (i)
      goals.push_back(
          {"compatible", "?component" + std::to_string(i - 1), component});
  }
  auto query = Json::array();
  for (auto &g : goals)
    query.push(g.data());
  auto result = native.query(catalog.facts, catalog.relations, Json::array(),
                             query, deadline, 2);
  if (result["status"].text() != "complete" || result["solutions"].size() == 0)
    throw Unsupported("No contract-compatible program construction exists.");
  std::vector<Claim> claims;
  for (auto f : catalog.facts.items())
    claims.push_back(Claim::from(f));
  ProofChecker checker(claims, catalog.relations, Json::array());
  auto row = result["solutions"].at(0),
       proof = checker.check(row["proofs"], goals, row["bindings"]);
  auto out = Json::object(), components = Json::array(), ops = Json::array();
  for (size_t i = 0; i < operations.size(); ++i) {
    components.push(row["bindings"]["?component" + std::to_string(i)]);
    ops.push(Json::string(operations[i]));
  }
  out.set("components", components);
  out.set("operations", ops);
  out.set("derivation", proof);
  out.set("engines", Json::parse("[\"souffle\",\"SWI-Prolog\"]"));
  return out;
}
} // namespace unllm::native
