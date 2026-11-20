#pragma once
#include "provider_types.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <pugixml.hpp>
#include <stdexcept>

namespace bricks {
using Groups = brick_data::Layers;
using NamedLists = std::map<std::string, std::vector<std::string>>;
inline QJsonDocument json_file(const char *path) {
  QFile file(QString::fromUtf8(path));
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Cannot open JSON");
  auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError)
    throw std::runtime_error("JSON read failed");
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError)
    throw std::invalid_argument(error.errorString().toStdString());
  return doc;
}
inline void save_json(const QJsonDocument &doc, const char *path) {
  QSaveFile file(QString::fromUtf8(path));
  auto bytes = doc.toJson(QJsonDocument::Compact);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    throw std::runtime_error("JSON write failed");
}
inline std::vector<std::string> string_array(const QJsonValue &value) {
  if (!value.isArray())
    throw std::invalid_argument("Expected an array");
  std::vector<std::string> out;
  for (auto item : value.toArray()) {
    if (!item.isString())
      throw std::invalid_argument("Expected a string");
    out.push_back(item.toString().toStdString());
  }
  return out;
}
template <class Range> QJsonArray array_value(const Range &values) {
  QJsonArray out;
  for (const auto &value : values)
    out.append(QString::fromStdString(value));
  return out;
}
template <class Map> Map named(const char *path) {
  auto doc = json_file(path);
  if (!doc.isObject())
    throw std::invalid_argument("Expected an object");
  auto object = doc.object();
  Map out;
  for (auto it = object.begin(); it != object.end(); ++it) {
    auto values = string_array(it.value());
    out.emplace(it.key().toStdString(),
                typename Map::mapped_type(values.begin(), values.end()));
  }
  return out;
}
template <class Map> void save_named(const Map &values, const char *path) {
  QJsonObject out;
  for (const auto &[name, items] : values)
    out.insert(QString::fromStdString(name), array_value(items));
  save_json(QJsonDocument(out), path);
}
inline Groups groups(const char *path) {
  auto doc = json_file(path);
  if (!doc.isArray())
    throw std::invalid_argument("Expected an array");
  Groups out;
  for (auto value : doc.array())
    out.push_back(string_array(value));
  return out;
}
inline void save_groups(const Groups &groups, const char *path) {
  QJsonArray out;
  for (const auto &group : groups)
    out.append(array_value(group));
  save_json(QJsonDocument(out), path);
}
inline std::vector<std::string> keys(const char *path) {
  auto doc = json_file(path);
  if (!doc.isObject())
    throw std::invalid_argument("Expected an object");
  std::vector<std::string> out;
  for (auto key : doc.object().keys())
    out.push_back(key.toStdString());
  std::sort(out.begin(), out.end());
  return out;
}
inline std::vector<std::string>
xml_values(const char *path, const std::string &xpath, bool names) {
  pugi::xml_document doc;
  if (!doc.load_file(path))
    throw std::runtime_error("Invalid or unreadable XML");
  auto nodes = doc.select_nodes(xpath.c_str());
  nodes.sort();
  std::vector<std::string> out;
  for (auto item : nodes) {
    if (auto attr = item.attribute())
      out.emplace_back(names ? attr.name() : attr.value());
    else
      out.emplace_back(names ? item.node().name() : item.node().text().get());
  }
  return out;
}
inline auto xml_names(const char *path, const std::string &xpath) {
  return xml_values(path, xpath, true);
}
inline auto xml_text(const char *path, const std::string &xpath) {
  return xml_values(path, xpath, false);
}
} // namespace bricks
namespace bricks {
inline std::vector<std::string> xml_paths(const char *path,
                                          const std::string &xpath) {
  pugi::xml_document doc;
  if (!doc.load_file(path))
    throw std::invalid_argument("Invalid XML");
  auto nodes = doc.select_nodes(xpath.c_str());
  nodes.sort();
  std::vector<std::string> out;
  for (auto item : nodes) {
    if (!item.node())
      throw std::invalid_argument("Expected nodes");
    out.push_back(item.node().path());
  }
  return out;
}
inline std::vector<std::string> xml_attributes(const char *path,
                                               const std::string &xpath) {
  pugi::xml_document doc;
  if (!doc.load_file(path))
    throw std::invalid_argument("Invalid XML");
  auto nodes = doc.select_nodes(xpath.c_str());
  nodes.sort();
  std::vector<std::string> out;
  for (auto item : nodes)
    for (auto attr : item.node().attributes())
      out.emplace_back(attr.name());
  return out;
}
} // namespace bricks
