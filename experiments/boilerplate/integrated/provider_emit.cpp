// Requirements contain no provider names or call order. Only trusted bundle
// metadata supplies C++ types, functions, and headers.
#include "provider_plan.h"
#include <algorithm>
#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace unllm::native;

namespace {
struct InvalidParameter : std::runtime_error {
  using std::runtime_error::runtime_error;
};
Json load(const char *path) { return Json::parse(read_file(path)); }
std::string quoted(const std::string &value) {
  std::ostringstream out;
  out << "std::string(\"" << std::oct << std::setfill('0');
  for (unsigned char c : value) {
    if (c == '"' || c == '\\')
      out << '\\' << char(c);
    else if (c >= 32 && c <= 126)
      out << char(c);
    else
      out << '\\' << std::setw(3) << unsigned(c);
  }
  out << "\", " << std::dec << value.size() << ")";
  return out.str();
}
std::string argument(const Json &type, Json value) {
  if (type.is_array()) {
    if (value.is_array() && value.size() == 1)
      value = value.at(0);
    for (auto option : type.items())
      if (value.is_string() && value.text() == option.text())
        return value.text();
  }
  if (type.text() == "text" && value.is_string())
    return quoted(value.text());
  if (type.text() == "integer" || type.text() == "column") {
    // A lossless decimal spelling is common in otherwise valid model JSON.
    if (value.is_string()) {
      auto spelling = value.text();
      std::int64_t parsed = 0;
      auto [end, error] = std::from_chars(
          spelling.data(), spelling.data() + spelling.size(), parsed);
      if (error == std::errc{} && end == spelling.data() + spelling.size() &&
          std::to_string(parsed) == spelling)
        value = Json::number(parsed);
    }
    if (value.is_integer() &&
        (type.text() != "column" || value.integer() >= 0)) {
      if (value.integer() == INT64_MIN)
        return "(-9223372036854775807LL-1)";
      return std::to_string(value.integer()) + "LL";
    }
  }
  if (type.text() == "comparison" && value.is_string()) {
    static const std::map<std::string, std::string> cmp{{"lt", "less"},
                                                        {"le", "less_equal"},
                                                        {"eq", "equal_to"},
                                                        {"ge", "greater_equal"},
                                                        {"gt", "greater"}};
    if (cmp.contains(value.text()))
      return "std::" + cmp.at(value.text()) + "<>{}";
  }
  throw Unsupported("Invalid or missing requirement parameter");
}
std::string value_type(const Json &schema, const std::string &port) {
  if (port.starts_with("in:") || port.starts_with("out:"))
    return "const char*";
  return required_text(schema["types"], port);
}

Json plan_role(const Json &schema, const std::set<std::string> &available,
               const std::set<std::string> &preferred, const Json &requirements,
               const Json &role, const std::string &mime, std::size_t slot,
               NativeEngine &engine) {
  auto residual = requirements["uncovered"].clone();
  if (!residual.is_array())
    throw Unsupported("Expected uncovered requirements");
  for (auto item : residual.items())
    if (!item.is_string() || item.text().empty())
      throw Unsupported("Invalid uncovered requirement");
  auto sequence = requirements["operations"];
  if (sequence.null()) {
    sequence = Json::array();
    sequence.push(requirements);
  } else if (requirements.contains("outcome") &&
             !requirements["outcome"].null()) {
    throw Unsupported("Ambiguous single and multiple outcomes");
  }
  if (!sequence.is_array() || !sequence.size() || sequence.size() > 32)
    throw Unsupported("Expected between one and 32 ordered outcomes");
  auto input = "in:" + mime,
       output = "out:" + required_text(role, "output_type");
  struct Binding {
    Json descriptor;
    std::map<std::string, std::string> arguments;
  };
  Names effects;
  std::map<std::string, Binding> descriptors;
  std::vector<PortProvider> catalog;
  for (std::size_t i = 0; i < sequence.size(); ++i) {
    auto op = sequence.at(i);
    auto effect = required_text(op, "outcome");
    auto definition = schema["outcomes"][effect];
    if (!definition.is_object())
      throw Unsupported("Unknown observable outcome: " + effect);
    effects.push_back(effect);
    auto params = op["parameters"], fields = definition["parameters"];
    if (!params.is_object())
      throw Unsupported("Expected outcome parameters");
    std::map<std::string, std::string> args;
    for (const auto &[key, value] : params.members())
      if (!fields.contains(key))
        residual.push(
            Json::string("Unverified parameter " + key + " = " + value.dump()));
    for (const auto &[key, type] : fields.members()) {
      try {
        args[key] = argument(type, params[key]);
      } catch (const Unsupported &) {
        throw InvalidParameter(effect + "." + key + " expects " + type.dump() +
                               ", received " + params[key].dump());
      }
    }
    for (auto p : schema["providers"].items()) {
      auto id = required_text(p, "id"), owner = required_text(p, "brick");
      auto from = required_text(p, "from"), to = required_text(p, "to"),
           behavior = p["effect"].text();
      if (!available.contains(owner) ||
          (from.starts_with("in:") && from != input) ||
          (to.starts_with("out:") && to != output))
        continue;
      if (behavior.empty() ? i != 0 : behavior != effect)
        continue;
      bool eligible = true;
      for (auto &[key, v] : p["when"].members())
        if (!args.contains(key) || args.at(key) != v.text())
          eligible = false;
      if (!eligible)
        continue;
      if (sequence.size() > 1 && !behavior.empty())
        id = "op_" + std::to_string(i) + "_" + id;
      catalog.push_back({id, from, to, behavior, preferred.contains(owner), i});
      descriptors.emplace(id, Binding{p, args});
    }
  }
  auto plan = checked_port_plan(catalog, input, output, effects, engine,
                                after_ms(15000));
  if (plan["status"].text() == "unsupported")
    throw Unsupported("No provider chain for requested outcomes: " +
                      sequence.dump());
  const auto fn = "component_" + std::to_string(slot);
  auto holes = Json::array(), ids = Json::array(), includes = Json::array();
  std::set<std::string> bricks, headers;
  std::ostringstream body;
  std::string variable = "input_path";
  int index = 0;
  for (auto step : plan["steps"].items()) {
    auto from = step["from"].text(), to = step["to"].text();
    bool sink = to == output;
    std::string call;
    if (step.contains("provider")) {
      const auto &binding = descriptors.at(step["provider"].text());
      const auto &p = binding.descriptor;
      headers.insert(required_text(p, "header"));
      bricks.insert(required_text(p, "brick"));
      call = required_text(p, "call") + "(";
      if (sink && p["destination_first"].flag())
        call += "output_path, ";
      call += variable;
      for (auto name : p["args"].items())
        call += ", " + binding.arguments.at(name.text());
      if (sink && !p["destination_first"].flag())
        call += ", output_path";
    } else {
      auto hole = Json::object();
      auto name = "gap_" + std::to_string(slot) + "_" + std::to_string(index);
      auto param = from == input
                       ? "const char* input_path"
                       : "const " + value_type(schema, from) + "& value";
      auto signature = "inline " + (sink ? "void" : value_type(schema, to)) +
                       " " + name + "(" + param +
                       (sink ? ", const char* output_path)" : ")");
      hole.set("name", Json::string(name));
      hole.set("signature", Json::string(signature));
      hole.set("from", step["from"]);
      hole.set("to", step["to"]);
      holes.push(hole);
      call = name + "(" + variable + (sink ? ", output_path" : "");
    }
    auto next = "value_" + std::to_string(++index);
    body << "    " << (sink ? "" : "auto " + next + " = ") << call << ");\n";
    variable = next;
  }
  std::ostringstream src;
  src << "#pragma once\n#include \"provider_types.hpp\"\n";
  for (const auto &h : headers) {
    src << "#include \"" << h << "\"\n";
    includes.push(Json::string(h));
  }
  if (holes.size())
    src << "#include \"gaps.hpp\"\n";
  src << "inline void " << fn
      << "(const char* input_path, const char* output_path) {\n"
      << body.str() << "}\n";
  for (const auto &id : bricks)
    ids.push(Json::string(id));
  auto result = Json::object();
  result.set("function", Json::string(fn));
  result.set("slot", Json::number(slot));
  result.set("source", Json::string(src.str()));
  result.set("bricks", ids);
  result.set("headers", includes);
  result.set("typed_gaps", holes);
  result.set("uncovered", residual);
  result.set(
      "status",
      Json::string(holes.size() || residual.size() ? "partial" : "complete"));
  result.set("plan", plan);
  return result;
}
} // namespace
int main(int argc, char **argv) try {
  if (argc != 9 && argc != 10)
    throw std::runtime_error(
        "Usage: brick-provider-plan ROOT WORKER CONTRACTS COMPONENTS PICKS "
        "SPEC ROUTE REQUIREMENTS [RUNTIME]");
  NativeEngine engine(argv[1], argv[2]);
  auto schema = load(argv[3]), components = load(argv[4]),
       selected = load(argv[5]);
  auto spec = load(argv[6]), route = load(argv[7]), requests = load(argv[8]);
  auto runtime = argc == 10 ? load(argv[9]) : Json::object();
  std::set<std::string> available, preferred;
  for (auto c : components.items())
    available.insert(required_text(c, "id"));
  for (auto c : selected.items())
    preferred.insert(required_text(c, "id"));
  auto result = Json::object(), providers = Json::array(),
       gaps = Json::object();
  auto issues = Json::object();
  auto roles = spec["roles"];
  for (std::size_t i = 0; i < roles.size(); ++i) {
    auto role = roles.at(i), row = route["rows"].at(i);
    auto id = required_text(role, "id");
    if (row["role"].text() != id)
      throw std::runtime_error("Route and role IDs differ");
    if (row["bound"].flag())
      continue;
    try {
      if (role["multiple"].flag() || role["content"].text() != "whole")
        throw Unsupported("Unsupported output shape");
      auto claims = requests[id]["runtime"];
      if (!claims.null() && !claims.is_array())
        throw Unsupported("Invalid runtime requirements");
      auto requirement = requests[id];
      auto residual = requirement["uncovered"];
      if (requirement.is_object() && residual.null()) {
        residual = Json::array();
        residual.push(Json::string("Interpreter did not explicitly account for "
                                   "additional constraints"));
      }
      auto recognized = Json::array();
      for (auto claim : claims.items()) {
        if (!claim.is_string() || claim.text().empty())
          throw Unsupported("Invalid runtime guarantee");
        if (runtime.contains(claim.text()))
          recognized.push(claim);
        else if (residual.is_array())
          residual.push(
              Json::string("Unverified runtime guarantee: " + claim.text()));
      }
      requirement.set("uncovered", residual);
      auto provider = plan_role(schema, available, preferred, requirement, role,
                                required_text(spec, "input_type"), i, engine);
      provider.set("runtime", recognized);
      providers.push(provider);
    } catch (const InvalidParameter &e) {
      issues.set(id, Json::string(e.what()));
      gaps.set(id, Json::string(e.what()));
    } catch (const Unsupported &e) {
      gaps.set(id, Json::string(e.what()));
    }
  }
  if (issues.size())
    result.set("interpretation_errors", issues);
  result.set("providers", providers);
  result.set("gaps", gaps);
  std::cout << result.dump() << '\n';
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
