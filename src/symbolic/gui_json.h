#pragma once
#include "unllm/symbolic/common.h"
#include <fstream>
namespace unllm::native::gui {
inline Json obj(std::initializer_list<std::pair<std::string, Json>> fields) {
  auto result = Json::object();
  for (auto &[name, value] : fields)
    result.set(name, value);
  return result;
}
inline Json arr(std::initializer_list<Json> values) {
  auto result = Json::array();
  for (auto &value : values)
    result.push(value);
  return result;
}
inline Json str(const std::string &s) { return Json::string(s); }
inline Json num(double n) { return Json::real(n); }
inline Json boolean(bool b) { return Json::boolean(b); }
inline Json with(const Json &base, const std::string &key, const Json &value) {
  auto result = base.clone();
  result.set(key, value);
  return result;
}
inline void write(const std::filesystem::path &path, const std::string &data) {
  std::ofstream out(path, std::ios::binary);
  out << data;
  out.close();
  if (!out)
    throw std::runtime_error("Cannot write job artifact: " + path.string());
}
} // namespace unllm::native::gui
