#include "unllm/symbolic/behaviour_expression.h"
#include <algorithm>
#include <charconv>
#include <set>
#include <unicode/locid.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>

namespace unllm::native {
namespace {
constexpr size_t text_budget = 16 * 1024 * 1024;
void append(std::string &out, std::string_view piece) {
  if (piece.size() > text_budget - out.size())
    throw ResourceLimit("Expression text result exceeds budget");
  out.append(piece);
}
bool whitespace(UChar32 c) {
  return u_isUWhiteSpace(c) || (c >= 0x1c && c <= 0x1f);
}
bool linebreak(UChar32 c) {
  return (c >= 10 && c <= 13) || (c >= 0x1c && c <= 0x1e) || c == 0x85 ||
         c == 0x2028 || c == 0x2029;
}
Json primitive(const std::string &name, const std::vector<Json> &args) {
  const auto &first = args.at(0);
  if (name == "records_count")
    return Json::real(first.size());
  if (name == "count_true") {
    size_t count = 0;
    for (auto record : first.items()) {
      auto field = record[args.at(1).text()];
      if (!field.is_boolean())
        throw Unsupported("Record is missing a Boolean field");
      count += field.flag();
    }
    return Json::real(count);
  }
  if (name == "number_text") {
    char buf[64];
    auto [end, ec] = std::to_chars(buf, buf + sizeof(buf), first.real(),
                                   std::chars_format::general, 12);
    if (ec != std::errc{})
      throw Unsupported("Number formatting failed");
    return Json::string(std::string(buf, end));
  }
  auto s = first.text();
  if (s.size() > text_budget)
    throw ResourceLimit("Expression text exceeds budget");
  if (name == "upper" || name == "lower") {
    auto u = icu::UnicodeString::fromUTF8(s);
    if (name == "upper")
      u.toUpper(icu::Locale::getRoot());
    else
      u.toLower(icu::Locale::getRoot());
    std::string out;
    u.toUTF8String(out);
    if (out.size() > text_budget)
      throw ResourceLimit("Expression text result exceeds budget");
    return Json::string(out);
  }
  if (name == "replace") {
    auto needle = args.at(1).text(), replacement = args.at(2).text();
    std::string out;
    if (needle.empty()) {
      append(out, replacement);
      for (int32_t end = 0; end < static_cast<int32_t>(s.size());) {
        int32_t start = end;
        UChar32 cp;
        U8_NEXT(s.data(), end, static_cast<int32_t>(s.size()), cp);
        if (cp < 0)
          throw Unsupported("Invalid UTF-8 state text");
        append(out, std::string_view(s).substr(start, end - start));
        append(out, replacement);
      }
    } else {
      size_t start = 0;
      for (;;) {
        size_t pos = s.find(needle, start);
        append(out, std::string_view(s).substr(
                        start, pos == std::string::npos ? pos : pos - start));
        if (pos == std::string::npos)
          break;
        append(out, replacement);
        start = pos + needle.size();
      }
    }
    return Json::string(out);
  }
  size_t chars = 0, words = 0, line_count = 0, start = 0, last = 0,
         first_nonspace = s.size();
  bool in_word = false;
  bool collect =
      name == "reverse_lines" || name == "sort_lines" || name == "unique_lines";
  std::vector<std::string_view> parts;
  auto line = [&](size_t end) {
    ++line_count;
    if (collect) {
      if (parts.size() >= 1000000)
        throw ResourceLimit("Line transformation exceeds budget");
      parts.push_back(std::string_view(s).substr(start, end - start));
    }
  };
  for (int32_t end = 0; end < static_cast<int32_t>(s.size());) {
    int32_t offset = end;
    UChar32 cp;
    U8_NEXT(s.data(), end, static_cast<int32_t>(s.size()), cp);
    if (cp < 0)
      throw Unsupported("Invalid UTF-8 state text");
    ++chars;
    if (!whitespace(cp)) {
      if (!in_word)
        ++words;
      in_word = true;
      first_nonspace = std::min(first_nonspace, static_cast<size_t>(offset));
      last = end;
    } else
      in_word = false;
    if (linebreak(cp)) {
      line(offset);
      if (cp == '\r' && end < static_cast<int32_t>(s.size()) &&
          s[end] == '\n') {
        ++chars;
        ++end;
      }
      start = end;
    }
  }
  if (start < s.size())
    line(s.size());
  if (name == "strip")
    return Json::string(last ? s.substr(first_nonspace, last - first_nonspace)
                             : "");
  if (name == "words")
    return Json::real(words);
  if (name == "chars")
    return Json::real(chars);
  if (name == "lines")
    return Json::real(line_count);
  if (!collect)
    throw Unsupported("Unknown behaviour primitive");
  if (name == "sort_lines")
    std::sort(parts.begin(), parts.end());
  else if (name == "reverse_lines")
    std::reverse(parts.begin(), parts.end());
  else {
    std::set<std::string_view> seen;
    std::erase_if(parts, [&](auto part) { return !seen.insert(part).second; });
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i)
      append(out, "\n");
    append(out, parts[i]);
  }
  return Json::string(out);
}
bool equal(const Json &a, const Json &b) {
  if (a.is_number() && b.is_number())
    return a.real() == b.real();
  if (a.type() != b.type())
    return false;
  if (a.is_object() || a.is_array()) {
    if (a.size() != b.size())
      return false;
    if (a.is_object()) {
      for (auto &[key, value] : a.members())
        if (!b.contains(key) || !equal(value, b[key]))
          return false;
    } else
      for (size_t i = 0; i < a.size(); ++i)
        if (!equal(a.at(i), b.at(i)))
          return false;
    return true;
  }
  return canonical(a) == canonical(b);
}
Json numeric(double value) {
  if (!std::isfinite(value) || std::abs(value) > 1e15)
    throw Unsupported("Numeric domain");
  return Json::real(value);
}
} // namespace
Json evaluate_behaviour(const BehaviourValue &v, const Json &state) {
  if (v.kind == "literal")
    return v.value;
  if (v.kind == "records")
    return Json::array();
  if (v.kind == "state" || v.kind == "prev" || v.kind == "next") {
    auto source = v.kind == "state" ? state : state[v.kind];
    if (!source.contains(v.name))
      throw Unsupported("Missing state value: " + v.name);
    return source[v.name];
  }
  auto a = evaluate_behaviour(v.args.at(0), state);
  if (v.kind == "conditional")
    return evaluate_behaviour(v.args.at(a.flag() ? 1 : 2), state);
  if (v.kind == "unary") {
    if (v.name == "not")
      return Json::boolean(!a.flag());
    if (v.name == "neg")
      return numeric(-a.real());
    if (v.name == "positive")
      return a;
  }
  if (v.kind == "primitive") {
    std::vector<Json> args{a};
    for (size_t i = 1; i < v.args.size(); ++i)
      args.push_back(evaluate_behaviour(v.args[i], state));
    return primitive(v.name, args);
  }
  if (v.kind != "binary")
    throw Unsupported("Unknown expression node");
  if ((v.name == "and" && !a.flag()) || (v.name == "or" && a.flag()))
    return a;
  auto b = evaluate_behaviour(v.args.at(1), state);
  if (v.name == "and" || v.name == "or")
    return b;
  if (v.name == "eq" || v.name == "ne")
    return Json::boolean(equal(a, b) == (v.name == "eq"));
  if (v.name == "add" && a.is_string()) {
    auto out = a.text();
    if (out.size() > text_budget)
      throw ResourceLimit("Expression text exceeds budget");
    append(out, b.text());
    return Json::string(out);
  }
  auto left = a.real(), right = b.real();
  if (v.name == "add")
    return numeric(left + right);
  if (v.name == "subtract")
    return numeric(left - right);
  if (v.name == "multiply")
    return numeric(left * right);
  if (v.name == "divide") {
    if (right == 0)
      throw Unsupported("Division by zero");
    return numeric(left / right);
  }
  if (v.name == "lt")
    return Json::boolean(left < right);
  if (v.name == "le")
    return Json::boolean(left <= right);
  if (v.name == "gt")
    return Json::boolean(left > right);
  if (v.name == "ge")
    return Json::boolean(left >= right);
  throw Unsupported("Unknown expression operation");
}
} // namespace unllm::native
