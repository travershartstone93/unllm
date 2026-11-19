#pragma once
#include "unllm/symbolic/common.h"
#include <vector>
namespace unllm::maintenance {
inline std::vector<std::vector<std::string>>
parse_delimited(const std::string &raw, char delimiter, bool skip_comments) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false, closed = false, started = false;
  auto push = [&] {
    fields.push_back(std::move(field));
    field.clear();
    started = false;
    closed = false;
  };
  for (size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if (skip_comments && !quoted && fields.empty() && !started &&
        field.empty() && c == '#') {
      auto nl = raw.find('\n', i);
      if (nl == raw.npos)
        break;
      i = nl;
      continue;
    }
    if (quoted) {
      if (c != '"')
        field += c;
      else if (i + 1 < raw.size() && raw[i + 1] == '"') {
        field += '"';
        ++i;
      } else {
        quoted = false;
        closed = true;
      }
    } else if (c == delimiter) {
      push();
    } else if (c == '\n' || c == '\r') {
      if (c == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
        ++i;
      if (fields.empty() && !started && field.empty())
        continue;
      push();
      rows.push_back(std::move(fields));
      fields.clear();
    } else if (!started && c == '"') {
      started = true;
      quoted = true;
    } else {
      if (closed)
        throw native::Unsupported("Unexpected characters after quoted field");
      field += c;
      started = true;
    }
  }
  if (quoted)
    throw native::Unsupported("Unclosed quote");
  if (started || !fields.empty()) {
    push();
    rows.push_back(std::move(fields));
  }
  return rows;
}
} // namespace unllm::maintenance
