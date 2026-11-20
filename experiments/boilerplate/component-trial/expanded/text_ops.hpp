#pragma once
#include "provider_types.hpp"
#include <QSaveFile>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
namespace bricks {
using Strings = std::vector<std::string>;
inline Strings lines(const char *path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open text");
  Strings out;
  for (std::string line; std::getline(input, line);)
    out.push_back(line);
  if (!input.eof() || input.bad())
    throw std::runtime_error("Read error");
  return out;
}
inline Strings decoded_lines(const brick_data::ByteFile &file) {
  return lines(file->c_str());
}
inline void save_lines(const Strings &lines, const char *path) {
  QSaveFile file(QString::fromUtf8(path));
  if (!file.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot open output");
  for (const auto &line : lines)
    if (file.write(line.data(), line.size()) !=
            static_cast<qint64>(line.size()) ||
        !file.putChar('\n'))
      throw std::runtime_error("Write error");
  if (!file.commit())
    throw std::runtime_error("Commit failed");
}
inline Strings sorted(Strings v) {
  std::stable_sort(v.begin(), v.end());
  return v;
}
template <class Seq> Seq reversed(Seq v) {
  std::reverse(v.begin(), v.end());
  return v;
}
inline Strings unique(const Strings &v) {
  std::set<std::string> seen;
  Strings out;
  std::copy_if(v.begin(), v.end(), std::back_inserter(out),
               [&](const auto &s) { return seen.insert(s).second; });
  return out;
}
inline Strings consecutive(Strings v) {
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}
inline Strings trim(Strings v) {
  for (auto &s : v) {
    auto first = s.find_first_not_of(" \t\r\n\f\v");
    s = first == s.npos
            ? ""
            : s.substr(first, s.find_last_not_of(" \t\r\n\f\v") - first + 1);
  }
  return v;
}
inline Strings nonempty(Strings v) {
  v.erase(std::remove(v.begin(), v.end(), ""), v.end());
  return v;
}
inline Strings contains_text(const Strings &v, const std::string &needle) {
  Strings out;
  std::copy_if(v.begin(), v.end(), std::back_inserter(out),
               [&](const auto &s) { return s.find(needle) != s.npos; });
  return out;
}
inline Strings prefix(const Strings &v, const std::string &prefix) {
  Strings out;
  std::copy_if(v.begin(), v.end(), std::back_inserter(out), [&](const auto &s) {
    return s.compare(0, prefix.size(), prefix) == 0;
  });
  return out;
}
inline Strings take_first(Strings v, std::size_t n) {
  if (n < v.size())
    v.resize(n);
  return v;
}
inline Strings take_last(Strings v, std::size_t n) {
  if (n < v.size())
    v.erase(v.begin(), v.end() - n);
  return v;
}
inline std::vector<std::int64_t> lengths(const Strings &v) {
  std::vector<std::int64_t> out;
  for (auto &s : v)
    out.push_back(s.size());
  return out;
}
inline std::vector<std::int64_t> count(const Strings &v) {
  return {static_cast<std::int64_t>(v.size())};
}
} // namespace bricks
