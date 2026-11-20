#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace brick_data {
using ByteFile = std::shared_ptr<const std::string>;
using Fields = std::vector<std::string>;
using Visitor = std::function<void(std::int64_t, const Fields&)>;
using Rows = std::function<void(const Visitor&)>;
using Graph = std::map<std::string, std::set<std::string>>;
using Layers = std::vector<std::vector<std::string>>;
}
