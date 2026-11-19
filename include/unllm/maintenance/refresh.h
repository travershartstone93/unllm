#pragma once
#include "sources.h"
#include <set>
namespace unllm::maintenance {
struct RefreshOptions {
  bool dry = false, full = false, repair = false;
  std::set<std::string> sources;
};
using DownloadFile = std::function<void(const std::string &, const Path &)>;
// Called with the update lock held. Metadata is obtained by check_sources.
void refresh(const Paths &, const Path &bin, const Path &embed,
             const Json &rows, const Json &state, const RefreshOptions &,
             const DownloadFile &transport = {});
} // namespace unllm::maintenance
