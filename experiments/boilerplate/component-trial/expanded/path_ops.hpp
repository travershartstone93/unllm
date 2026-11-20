#pragma once
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
namespace bricks {
namespace fs = std::filesystem;
using Paths = std::vector<std::string>;
template <class Iterator, class Predicate>
Paths entries(const char *root, Predicate keep) {
  Paths out;
  for (const auto &e : Iterator(root))
    if (keep(e))
      out.push_back(e.path().lexically_relative(root).generic_string());
  std::sort(out.begin(), out.end());
  return out;
}
inline Paths files(const char *root) {
  return entries<fs::directory_iterator>(
      root, [](auto &e) { return fs::is_regular_file(e.symlink_status()); });
}
inline Paths directories(const char *root) {
  return entries<fs::directory_iterator>(
      root, [](auto &e) { return fs::is_directory(e.symlink_status()); });
}
inline Paths tree(const char *root) {
  return entries<fs::recursive_directory_iterator>(
      root, [](auto &e) { return fs::is_regular_file(e.symlink_status()); });
}
inline Paths links(const char *root) {
  return entries<fs::recursive_directory_iterator>(
      root, [](auto &e) { return e.is_symlink(); });
}
inline Paths suffix(const Paths &paths, const std::string &ext) {
  Paths out;
  std::copy_if(paths.begin(), paths.end(), std::back_inserter(out),
               [&](auto &p) { return fs::path(p).extension() == ext; });
  return out;
}
template <fs::path (fs::path::*member)() const>
Paths path_parts(const Paths &paths) {
  Paths out;
  for (auto &p : paths)
    out.push_back((fs::path(p).*member)().generic_string());
  return out;
}
inline Paths basename(const Paths &p) {
  return path_parts<&fs::path::filename>(p);
}
inline Paths stem(const Paths &p) { return path_parts<&fs::path::stem>(p); }
inline Paths extension(const Paths &p) {
  return path_parts<&fs::path::extension>(p);
}
inline Paths parent(const Paths &p) {
  return path_parts<&fs::path::parent_path>(p);
}
inline Paths normalize(const Paths &p) {
  return path_parts<&fs::path::lexically_normal>(p);
}
} // namespace bricks
