// Bind a single documented GLib UTF-8 -> owned string-vector function to a
// typed file input.
#include "unllm/symbolic/common.h"
#include <cctype>
#include <iostream>
#include <map>
#include <pugixml.hpp>
#include <regex>
#include <sstream>

using namespace unllm::native;
namespace {
bool mentions_format(const std::string &doc, const std::string &mime) {
  const auto token = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           std::string_view("/.+_-").find(c) != std::string_view::npos;
  };
  for (auto pos = doc.find(mime); pos != doc.npos;
       pos = doc.find(mime, pos + 1)) {
    const auto end = pos + mime.size();
    if ((pos == 0 || !token(doc[pos - 1])) &&
        (end == doc.size() || !token(doc[end])))
      return true;
  }
  return false;
}

std::string render(const std::string &symbol, bool per_line) {
  std::ostringstream s;
  s << R"(#pragma once
#include <glib.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
namespace unllm_generated {
inline std::vector<std::string> read_)"
    << symbol << R"((const char* path) {
    gchar* raw = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(path, &raw, &length, nullptr))
        throw std::runtime_error("Cannot read input file");
    std::unique_ptr<gchar, decltype(&g_free)> contents(raw, g_free);
    if (std::memchr(raw, 0, length) || !g_utf8_validate(raw, -1, nullptr))
        throw std::runtime_error("Expected UTF-8 text without embedded NUL");
    std::vector<std::string> result;
    auto append = [&](const char* text) {
        std::unique_ptr<gchar*, decltype(&g_strfreev)> values()"
    << symbol << R"((text), g_strfreev);
        if (values)
            for (auto p = values.get(); *p; ++p) result.emplace_back(*p);
    };
)";
  if (per_line)
    s << R"(    std::istringstream input{std::string(raw, length)};
    for (std::string line; std::getline(input, line);) append(line.c_str());
)";
  else
    s << "    append(raw);\n";
  s << R"(    return result;
}
}
)";
  return s.str();
}
} // namespace

int main(int argc, char **argv) try {
  if (argc != 4)
    throw std::runtime_error(
        "Usage: brick-gir-adapter TYPED_GAPS GLIB_GIR whole-file|per-line");
  const std::string mode = argv[3];
  if (mode != "whole-file" && mode != "per-line")
    throw Unsupported("Unknown invocation mode");
  const auto gaps = Json::parse(read_file(argv[1]));
  if (!gaps.is_object() || gaps.size() != 1)
    throw Unsupported("Expected one missing conversion");
  const auto gap = gaps.members().front().second;
  const auto from = required_text(gap, "from");
  if (!from.starts_with("in:text/") || required_text(gap, "to") != "strings")
    throw Unsupported("No emitter for this conversion");
  const auto mime = from.substr(3);
  const auto xml = read_file(argv[2]);
  pugi::xml_document gir;
  if (!gir.load_buffer(xml.data(), xml.size()) ||
      !gir.select_node("/repository/namespace[@name='GLib']") ||
      !gir.select_node("/repository/package[@name='glib-2.0']"))
    throw Unsupported("Expected GLib introspection metadata");
  // Ignore aliases of the same C symbol. Check ownership, arity and termination
  // before emitting a call.
  const auto candidates = gir.select_nodes(
      "/repository/namespace//function[not(@throws='1') and "
      "not(@introspectable='0') and not(@deprecated='1')"
      " and count(parameters/*)=1 and parameters/parameter[not(@direction) or "
      "@direction='in']/type[@name='utf8' and @c:type='const gchar*']"
      " and return-value[@transfer-ownership='full']/array[@c:type='gchar**' "
      "and not(@length) and not(@fixed-size)"
      " and not(@zero-terminated='0')]/type[@name='utf8']]");
  std::map<std::string, std::string> matches;
  for (auto item : candidates) {
    const auto fn = item.node();
    std::string symbol = fn.attribute("c:identifier").value(),
                doc = fn.child_value("doc");
    if (mentions_format(doc, mime) &&
        std::regex_match(symbol, std::regex("g_[a-z0-9_]+")))
      matches.emplace(symbol, doc);
  }
  if (matches.size() != 1)
    throw Unsupported("Expected one documented API for " + mime + ", found " +
                      std::to_string(matches.size()));
  const auto &[symbol, doc] = *matches.begin();
  const auto id = "gir_" + symbol;
  auto out = Json::object();
  for (const auto &[key, value] : std::map<std::string, std::string>{
           {"id", id},
           {"from", from},
           {"to", "strings"},
           {"symbol", symbol},
           {"header", id + ".hpp"},
           {"call", "unllm_generated::read_" + symbol},
           {"description", doc},
           {"package", "glib-2.0"},
           {"metadata_sha256", sha256(xml)},
           {"mode", mode},
           {"source", render(symbol, mode == "per-line")}})
    out.set(key, Json::string(value));
  std::cout << out.dump() << '\n';
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
