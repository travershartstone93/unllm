// Contract interpreter, independent of source emission and generated execution.
#include "unllm/symbolic/stream_contract.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <random>
#include <sstream>
namespace unllm::native {
namespace {
double checked(double n) {
  if (!std::isfinite(n) || std::abs(n) > 1e15)
    throw Unsupported("Numeric contract domain exceeded");
  return n;
}
std::string trim(const std::string &s) {
  auto a = s.find_first_not_of(" \t\r\n\v\f");
  if (a == std::string::npos)
    return "";
  auto b = s.find_last_not_of(" \t\r\n\v\f");
  return s.substr(a, b - a + 1);
}
std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> out;
  size_t begin = 0;
  for (;;) {
    auto end = s.find(delim, begin);
    out.push_back(
        s.substr(begin, end == std::string::npos ? end : end - begin));
    if (end == std::string::npos)
      return out;
    begin = end + 1;
  }
}
double number(const std::string &raw) {
  auto text = trim(raw);
  if (text.empty() || text.find_first_of("_xXpP") != std::string::npos ||
      text.find('\0') != std::string::npos)
    throw Unsupported("Expected decimal input");
  char *end = nullptr;
  double n = std::strtod(text.c_str(), &end);
  if (end != text.c_str() + text.size())
    throw Unsupported("Expected decimal input");
  return checked(n);
}
// Keep arithmetic as binary64, including intermediate infinity; the stream
// contract checks map results and numeric outputs, as the reference does.
struct Scalar {
  double number = 0;
  bool boolean = false;
  bool logical = false;
};
Scalar evaluate(const Expr &e, double x) {
  if (e.op == "variable")
    return {x};
  if (e.op == "constant")
    return e.value.is_boolean() ? Scalar{0, e.value.flag(), true}
                                : Scalar{e.value.real()};
  auto a = evaluate(e.args[0], x);
  if (e.op == "not")
    return {0, !a.boolean, true};
  if (e.op == "negate")
    return {-a.number};
  if (e.op == "if")
    return evaluate(e.args[a.boolean ? 1 : 2], x);
  if (e.op == "and" && !a.boolean)
    return {0, false, true};
  if (e.op == "or" && a.boolean)
    return {0, true, true};
  auto b = evaluate(e.args[1], x);
  if (e.op == "and" || e.op == "or")
    return b;
  if (e.op == "add")
    return {a.number + b.number};
  if (e.op == "subtract")
    return {a.number - b.number};
  if (e.op == "multiply")
    return {a.number * b.number};
  if (e.op == "gt")
    return {0, a.number > b.number, true};
  if (e.op == "ge")
    return {0, a.number >= b.number, true};
  if (e.op == "lt")
    return {0, a.number < b.number, true};
  if (e.op == "le")
    return {0, a.number <= b.number, true};
  if (e.op == "eq")
    return {0, a.number == b.number, true};
  throw Unsupported("Unknown contract expression");
}
using Values = std::vector<Json>;
Values transform(Values values, const Program &p) {
  for (auto &s : p.steps) {
    if (s.op == "filter")
      std::erase_if(
          values, [&](auto v) { return !evaluate(*s.expr, v.real()).boolean; });
    else if (s.op == "map")
      for (auto &v : values)
        v = Json::real(checked(evaluate(*s.expr, v.real()).number));
    else if (s.op == "unique") {
      Values distinct;
      for (auto &v : values)
        if (std::none_of(distinct.begin(), distinct.end(), [&](auto &previous) {
              return v.is_string() ? v.text() == previous.text()
                                   : v.real() == previous.real();
            }))
          distinct.push_back(v);
      values = std::move(distinct);
    } else if (s.op == "sort")
      std::stable_sort(values.begin(), values.end(), [&](auto &a, auto &b) {
        return a.is_string()
                   ? (s.descending ? a.text() > b.text() : a.text() < b.text())
                   : (s.descending ? a.real() > b.real() : a.real() < b.real());
      });
    else if (s.op == "reverse")
      std::reverse(values.begin(), values.end());
    else if (s.op == "strip")
      for (auto &v : values)
        v = Json::string(trim(v.text()));
    else if (s.op == "lowercase")
      for (auto &v : values) {
        auto text = v.text();
        for (char &ch : text)
          if (ch >= 'A' && ch <= 'Z')
            ch += 32;
        v = Json::string(text);
      }
    else if (s.op == "words") {
      Values words;
      for (auto v : values) {
        std::istringstream in(v.text());
        std::string word;
        while (in >> word)
          words.push_back(Json::string(word));
      }
      values = std::move(words);
    }
  }
  return values;
}
Json row(std::initializer_list<Json> values) {
  auto out = Json::array();
  for (auto &v : values)
    out.push(v);
  return out;
}
Json render(const Values &values, const Program &p) {
  auto out = Json::array();
  if (p.output == "values") {
    for (auto &v : values)
      out.push(row({v}));
    return out;
  }
  if (p.output == "count") {
    out.push(row({Json::number(values.size())}));
    return out;
  }
  if (p.output == "frequencies") {
    std::map<std::string, int64_t> counts;
    for (auto &v : values) {
      auto text = v.text();
      if (text.find('\t') != std::string::npos)
        throw Unsupported("Frequency key contains tabs");
      ++counts[text];
    }
    out.push(row({Json::string("value"), Json::string("count")}));
    for (auto &[key, n] : counts)
      out.push(row({Json::string(key), Json::number(n)}));
    return out;
  }
  if (values.empty()) {
    if (p.output == "sum") {
      out.push(row({Json::number(0)}));
      return out;
    }
    throw Unsupported("No numeric values remain");
  }
  double sum = 0, minimum = values.front().real(), maximum = minimum;
  for (auto v : values) {
    double n = v.real();
    sum += n;
    minimum = std::min(minimum, n);
    maximum = std::max(maximum, n);
  }
  if (p.output == "sum")
    out.push(row({Json::real(checked(sum))}));
  else if (p.output == "mean")
    out.push(row({Json::real(checked(sum / values.size()))}));
  else if (p.output == "min")
    out.push(row({Json::real(minimum)}));
  else if (p.output == "max")
    out.push(row({Json::real(maximum)}));
  else
    out.push(row({Json::number(values.size()), Json::real(checked(sum)),
                  Json::real(minimum), Json::real(maximum),
                  Json::real(checked(sum / values.size()))}));
  return out;
}
} // namespace
Json stream_reference(const Program &program, const std::string &input) {
  auto p = Program::from(program.data());
  auto lines = input.empty() ? std::vector<std::string>{} : split(input, '\n');
  if (input.ends_with('\n'))
    lines.pop_back();
  if (lines.size() > 100000)
    throw Unsupported("Row budget exceeded");
  Values values;
  std::map<std::string, Values> groups;
  for (auto line : lines) {
    try {
      if (line.ends_with('\r'))
        line.pop_back();
      if (line.size() > 16384)
        throw Unsupported("Line length exceeded");
      for (unsigned char c : line)
        if (c >= 127 || (c < 32 && c != '\t'))
          throw Unsupported("Printable ASCII and tabs required");
      if (p.input == "lines") {
        values.push_back(Json::string(line));
        continue;
      }
      if (trim(line).empty())
        continue;
      if (p.input == "records") {
        auto fields = split(line, '\t');
        if (fields.size() != 2 || trim(fields[0]).empty())
          throw Unsupported("Invalid record shape");
        auto n = number(fields[1]);
        groups[trim(fields[0])].push_back(Json::real(n));
      } else
        values.push_back(Json::real(number(line)));
    } catch (const Unsupported &) {
      if (p.invalid != "skip")
        throw;
    }
  }
  auto out = Json::array();
  if (p.output == "stats") {
    auto header = Json::array();
    if (p.input == "records")
      header.push(Json::string("key"));
    for (auto h : {"count", "sum", "min", "max", "mean"})
      header.push(Json::string(h));
    out.push(header);
  }
  if (p.input == "records") {
    for (auto &[group, items] : groups) {
      auto result = transform(items, p);
      if (result.empty())
        continue;
      for (auto cells : render(result, p).items()) {
        auto r = Json::array();
        r.push(Json::string(group));
        for (auto cell : cells.items())
          r.push(cell);
        out.push(r);
      }
    }
  } else
    for (auto r : render(transform(values, p), p).items())
      out.push(r);
  return out;
}
std::vector<std::string> stream_cases(const Program &p) {
  std::vector<std::string> cases{"", "\n"};
  if (p.input == "lines") {
    for (auto text :
         {"one two\nTwo three\n", " A \nA\n a \n", "a\tb\n", "café\n"})
      cases.push_back(text);
    cases.push_back(std::string("a\0b\n", 4));
    cases.push_back(std::string(16385, 'x') + "\n");
  } else {
    auto record = [&](const std::string &n) {
      return p.input == "records" ? "group\t" + n : n;
    };
    for (auto n : {"0", "-0.0", "-5", "8", "1e15", "-1e15", "1e16", "nan",
                   "inf", "1_000", "0x10", "bad", "1e-300"})
      cases.push_back(record(n) + "\n");
    cases.push_back(record("2") + "\r\n" + record("-3") + "\n");
    cases.push_back(record("bad") + "\n" + record("4") + "\n");
    if (p.input == "records") {
      cases.push_back("a\t2\nb\t3\na\t-1\n");
      cases.push_back("\t2\n");
      cases.push_back("a\t2\t3\n");
    }
  }
  std::mt19937 random(47927);
  for (int i = 0; i < 20; ++i) {
    std::string text;
    for (unsigned n = 0, count = random() % 13; n < count; ++n) {
      if (p.input == "lines") {
        for (unsigned k = 0, len = random() % 16; k < len; ++k)
          text += "abcABC 012"[random() % 10];
      } else {
        if (p.input == "records")
          text += std::string(1, char('a' + random() % 3)) + "\t";
        int value = int(random() % 101) - 50;
        text += std::to_string(value);
        if (random() % 3 == 0)
          text += ".5";
      }
      text += '\n';
    }
    cases.push_back(text);
  }
  return cases;
}
} // namespace unllm::native
