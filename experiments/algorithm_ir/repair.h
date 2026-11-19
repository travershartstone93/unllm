#pragma once
#include "ir.h"
#include <functional>
namespace unllm::algorithm {
// Visit well-scoped, single-site edits of a supplied tree, one at a time.
bool visit_edits(
    const Json &tree,
    const std::function<bool(const Json &, const std::string &)> &visit);
} // namespace unllm::algorithm
