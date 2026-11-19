#include "unllm/symbolic/mrs.h"
#include "AceProtocolLexer.h"
#include "AceProtocolParser.h"
#include "SimpleMrsLexer.h"
#include "SimpleMrsParser.h"
#include <algorithm>

namespace unllm::native {
namespace {
struct Errors : antlr4::BaseErrorListener {
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, size_t line,
                   size_t column, const std::string &message,
                   std::exception_ptr) override {
    throw Unsupported("Structured parser error at " + std::to_string(line) +
                      ":" + std::to_string(column) + ": " + message);
  }
};
std::string lower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  return s;
}
std::string upper(std::string s) {
  for (char &c : s)
    if (c >= 'a' && c <= 'z')
      c -= 32;
  return s;
}
std::string unquote(const std::string &s) {
  if (s.size() < 2 || s.front() != '"' || s.back() != '"')
    throw Unsupported("Invalid quoted token");
  std::string out;
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    if (s[i] == '\\') {
      if (++i + 1 >= s.size())
        throw Unsupported("Truncated escape");
    }
    out += s[i];
  }
  return out;
}
void bounded(const std::string &text) {
  if (text.size() > 16 * 1024 * 1024)
    throw ResourceLimit("Parser input limit exceeded");
  int depth = 0;
  bool quoted = false, escaped = false;
  for (char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"')
      quoted = true;
    else if (c == '[' || c == '(') {
      if (++depth > 128)
        throw ResourceLimit("Structured input nesting exceeded");
    } else if (c == ']' || c == ')')
      --depth;
  }
}
std::string variable(SimpleMrsParser::VariableContext *node, Mrs &m) {
  auto name = lower(node->ATOM(0)->getText());
  auto &props = m.variables[name];
  for (auto *p : node->property()) {
    auto k = upper(p->ATOM(0)->getText()), v = lower(p->ATOM(1)->getText());
    auto old = props.find(k);
    if (old != props.end() && old->second != v)
      throw Unsupported("Conflicting MRS variable property");
    props[k] = v;
  }
  return name;
}
std::string atom(AceProtocolParser::ValueContext *v) {
  return v->ATOM() ? v->ATOM()->getText() : "";
}
} // namespace
Json Predication::data() const {
  Json out = Json::object(), a = Json::object();
  for (auto &[k, v] : args)
    a.set(k, Json::string(v));
  out.set("predicate", Json::string(predicate));
  out.set("label", Json::string(label));
  out.set("args", a);
  out.set("span", span);
  return out;
}
Json Mrs::data() const {
  Json out = Json::object(), r = Json::array(), vars = Json::object();
  for (auto &e : rels)
    r.push(e.data());
  for (auto &[v, props] : variables) {
    auto p = Json::object();
    for (auto &[k, s] : props)
      p.set(k, Json::string(s));
    vars.set(v, p);
  }
  out.set("top", top.empty() ? Json{} : Json::string(top));
  out.set("index", index.empty() ? Json{} : Json::string(index));
  out.set("rels", r);
  out.set("variables", vars);
  out.set("hcons", hcons);
  out.set("icons", icons);
  return out;
}
Mrs decode_mrs(const std::string &source) {
  bounded(source);
  antlr4::ANTLRInputStream input(source);
  SimpleMrsLexer lexer(&input);
  Errors errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  antlr4::CommonTokenStream tokens(&lexer);
  SimpleMrsParser parser(&tokens);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  auto *tree = parser.document();
  Mrs m;
  for (auto *h : tree->header()) {
    auto name = h->children[0]->getText();
    if (name == "TOP" || name == "LTOP")
      m.top = lower(h->ATOM()->getText());
    else if (name == "INDEX")
      m.index = variable(h->variable(), m);
    else if (name == "RELS")
      for (auto *r : h->relation()) {
        Predication ep;
        auto p = r->predicate()->getText();
        if (p.starts_with('"'))
          p = unquote(p);
        else if (p.starts_with('\''))
          p.erase(0, 1);
        if (p.ends_with("_rel"))
          p.resize(p.size() - 4);
        ep.predicate = lower(p);
        ep.label = lower(r->ATOM()->getText());
        m.variables.try_emplace(ep.label);
        if (r->LINK()) {
          auto link = r->LINK()->getText();
          auto separator = link.find(':');
          if (separator == std::string::npos)
            throw Unsupported("MRS link kind needs an explicit adapter");
          ep.span = Json::array();
          ep.span.push(Json::number(std::stoll(link.substr(1, separator - 1))));
          ep.span.push(Json::number(std::stoll(
              link.substr(separator + 1, link.size() - separator - 2))));
        }
        for (auto *a : r->argument()) {
          auto k = upper(a->ATOM()->getText());
          if (ep.args.contains(k))
            throw Unsupported("Duplicate MRS argument");
          ep.args[k] = a->STRING() ? unquote(a->STRING()->getText())
                                   : variable(a->variable(), m);
        }
        m.rels.push_back(std::move(ep));
      }
    else
      for (auto *c : h->constraint()) {
        auto left = variable(c->variable(0), m),
             right = variable(c->variable(1), m);
        auto item = Json::object();
        item.set(name == "HCONS" ? "hi" : "left", Json::string(left));
        item.set(name == "HCONS" ? "lo" : "right", Json::string(right));
        item.set("relation", Json::string(lower(c->ATOM()->getText())));
        (name == "HCONS" ? m.hcons : m.icons).push(item);
      }
  }
  if (!m.top.empty())
    m.variables.try_emplace(m.top);
  return m;
}
ParseResult decode_ace_response(const std::string &response) {
  bounded(response);
  antlr4::ANTLRInputStream input(response);
  AceProtocolLexer lexer(&input);
  Errors errors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&errors);
  antlr4::CommonTokenStream tokens(&lexer);
  AceProtocolParser parser(&tokens);
  parser.removeErrorListeners();
  parser.addErrorListener(&errors);
  auto *tree = parser.document();
  ParseResult result;
  for (auto *v : tree->value()) {
    auto values = v->value();
    if (values.size() == 2 && atom(values[0]) == ":error" && values[1]->STRING())
      result.error = unquote(values[1]->STRING()->getText());
    if (values.size() != 2 || atom(values[0]) != ":results")
      continue;
    for (auto *row : values[1]->value())
      for (auto *pair : row->value()) {
        auto entries = pair->value();
        if (entries.size() == 2 && atom(entries[0]) == ":mrs") {
          if (!entries[1]->STRING())
            throw Unsupported("ACE MRS value is not a string");
          result.analyses.push_back(decode_mrs(unquote(entries[1]->STRING()->getText())));
        }
      }
  }
  return result;
}
AceClient::AceClient(const std::filesystem::path &root, int timeout,
                     int maximum, const std::string &grammar_root)
    : process_({"/usr/bin/env", "LC_ALL=C.UTF-8", (root / ".deps/ace-0.9.34/ace").string(), "--maxent=/dev/null",
                "-g", (root / ".deps/erg.dat").string(), "-n",
                std::to_string(maximum), "-r", grammar_root,
                "--timeout=" + std::to_string(timeout), "--tsdb-notes",
                "--tsdb-stdout", "--report-labels"}),
      timeout_(timeout), maximum_(maximum) {}
ParseResult AceClient::parse(const std::string &text, Deadline outer_deadline) {
  if (text.empty() || text.size() > 4000 ||
      text.find_first_of("\r\n") != std::string::npos ||
      text.find('\0') != std::string::npos)
    throw Unsupported("Expected one bounded input sentence");
  auto deadline = std::min(outer_deadline, after_ms(timeout_ * 1000 + 5000));
  process_.reset_output_budget();
  process_.write(text + "\n", deadline);
  ParseResult result;
  int blanks = 0;
  while (blanks < 2) {
    auto s = process_.line(deadline);
    if (s.empty()) {
      ++blanks;
      continue;
    }
    if (s.starts_with("NOTE:") || s.starts_with("WARNING:") ||
        s.starts_with("ERROR:") || s.starts_with("SENT:") ||
        s.starts_with("SKIP:")) {
      result.notes += s + "\n";
      continue;
    }
    result.raw += s + "\n";
  }
  auto decoded = decode_ace_response(result.raw);
  result.analyses = std::move(decoded.analyses);
  result.error = std::move(decoded.error);
  result.reached_candidate_limit =
      result.analyses.size() >= static_cast<size_t>(maximum_);
  return result;
}
} // namespace unllm::native
