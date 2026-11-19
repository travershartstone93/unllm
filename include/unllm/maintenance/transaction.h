#pragma once
#include "sources.h"
namespace unllm::maintenance {
// One writer holds this lock for recovery, staging and publication.
class UpdateLock {
  int fd_;

public:
  explicit UpdateLock(const Paths &);
  UpdateLock(const UpdateLock &) = delete;
  UpdateLock &operator=(const UpdateLock &) = delete;
  ~UpdateLock();
};
using Progress = std::function<void(size_t)>;
void recover_update(const Paths &);
void publish_verified(const Paths &, const std::vector<std::string> &packs,
                      const std::string &index, Progress after_move = {});
bool rollback_update(const Paths &, Progress after_move = {});
bool prune_update(const Paths &, bool only_if_expired);
} // namespace unllm::maintenance
