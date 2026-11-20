#include "provider_plan.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>

using namespace unllm::native;
namespace fs = std::filesystem;
namespace {
bool public_identifier(const std::string &name) {
  static const std::regex pattern("[A-Za-z][A-Za-z0-9_]*");
  return std::regex_match(name, pattern) && name.find("__") == name.npos;
}
Json load(const fs::path &p) { return Json::parse(read_file(p.string())); }
} // namespace
int main(int argc, char **argv) try {
  if (argc != 4)
    throw std::runtime_error("Usage: brick-header-emit BUNDLE SPEC PICKS");
  const fs::path bundle = argv[1];
  auto catalog = load(bundle / "providers/library.json"),
       components = load(bundle / "components.json");
  auto artifact = load(argv[2])["artifact"];
  auto file = required_text(artifact, "filename"),
       name = required_text(artifact, "class_name");
  auto value = required_text(artifact, "value_type"),
       cpp_type = required_text(catalog["value_types"], value);
  if (!public_identifier(name) ||
      !std::regex_match(file,
                        std::regex("[A-Za-z0-9_][A-Za-z0-9_.-]*\\.(hpp|h|hh)")))
    throw Unsupported("Invalid requested class or header name");
  auto methods = artifact["methods"], residual = artifact["uncovered"];
  if (methods.is_object()) {
    auto named = Json::array();
    for (const auto &[public_name, operation] : methods.members()) {
      auto method = Json::object();
      method.set("name", Json::string(public_name));
      method.set("operation", operation);
      named.push(method);
    }
    methods = named;
  }
  if (!methods.is_array() || !methods.size() || !residual.is_array())
    throw Unsupported("Incomplete library specification");
  for (auto r : residual.items())
    if (!r.is_string() || r.text().empty())
      throw Unsupported("Invalid unresolved requirement");
  std::set<std::string> available, preferred;
  for (auto c : components.items())
    available.insert(required_text(c, "id"));
  for (auto p : load(argv[3]).items())
    preferred.insert(required_text(p, "id"));
  Json choice;
  for (auto impl : catalog["classes"].items()) {
    auto id = required_text(impl, "brick");
    if (!available.contains(id))
      continue;
    bool match = true;
    for (auto m : methods.items())
      if (!impl["methods"].contains(required_text(m, "operation")))
        match = false;
    if (match && (choice.null() || preferred.contains(id)))
      choice = impl;
    if (match && preferred.contains(id))
      break;
  }
  if (choice.null())
    throw Unsupported("No registered class implements all requested methods");
  std::set<std::string> used{name};
  std::ostringstream code;
  auto header = required_text(choice, "header");
  const auto implementation = read_file((bundle / "headers" / header).string());
  // Two public headers can embed the same reusable implementation.
  const auto guard = "UNLLM_BRICK_" + sha256(implementation);
  code << "#pragma once\n#ifndef " << guard << "\n#define " << guard << "\n"
       << implementation << "\n#endif\n#include <string>\n#include <cstdint>\nclass "
       << name << " {\n";
  std::string member = "implementation_";
  for (auto method : methods.items())
    used.insert(required_text(method, "name"));
  while (used.contains(member))
    member += 'x';
  used = {name};
  code << "    " << required_text(choice, "type") << "<" << cpp_type << "> "
       << member << ";\npublic:\n";
  for (auto method : methods.items()) {
    auto public_name = required_text(method, "name"),
         op = required_text(method, "operation");
    if (!public_identifier(public_name) || !used.insert(public_name).second)
      throw Unsupported("Invalid or duplicated public method");
    auto binding = choice["methods"][op];
    bool arg = required_text(binding, "parameter") == "value";
    std::string result = required_text(binding, "result") == "optional"
                             ? "std::optional<" + cpp_type + ">"
                             : "bool";
    code << "    " << result << " " << public_name << "("
         << (arg ? "const " + cpp_type + "& value" : "") << ") { return "
         << member << "." << required_text(binding, "member") << "("
         << (arg ? "value" : "") << "); }\n";
  }
  code << "};\n";
  auto result = Json::object();
  result.set("filename", Json::string(file));
  result.set("brick", choice["brick"]);
  result.set("source", Json::string(code.str()));
  result.set("uncovered", residual);
  std::cout << result.dump() << "\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
