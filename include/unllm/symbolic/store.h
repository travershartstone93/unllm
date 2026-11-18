#pragma once
#include "model.h"
#include <sqlite3.h>
namespace unllm::native {
class FactStore {
public:
  FactStore(const std::filesystem::path &path,
            const std::filesystem::path &resources);
  ~FactStore();
  FactStore(const FactStore &) = delete;
  FactStore &operator=(const FactStore &) = delete;
  int64_t revision() const;
  Json status() const;
  Json relations() const;
  Json entities() const;
  std::vector<std::string> aliases() const;
  Json resolve(const std::string &label) const;
  Json source(const std::string &id) const;
  std::vector<Claim> claims(int64_t now) const;
  Json completeness() const;
  Json prepared(const std::string &id) const;
  void save_prepared(const std::string &id, const Json &snapshot);
  Json import_document(const Json &doc);
  void begin_read();
  void end_read();

private:
  sqlite3 *db_ = nullptr;
  bool reading_ = false;
  void exec(const std::string &sql) const;
  Json rows(const std::string &sql,
            const std::vector<Json> &bindings = {}) const;
  void execute(const std::string &sql,
               const std::vector<Json> &bindings = {}) const;
};
} // namespace unllm::native
