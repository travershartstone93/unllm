#pragma once
#include "ir.h"
#include <functional>
namespace unllm::algorithm {
// A successful search returns a typed candidate and its evidence on the
// supplied examples.
Json synthesize(const Json &contract, const Json &previous = {});
// Development trials may propose a bounded shortlist of typed element
// expressions.
using FragmentSource = std::function<std::vector<Json>(const Json &)>;
Json synthesize_with_fragments(const Json &contract,
                               const FragmentSource &source,
                               const std::vector<std::string> &operations = {});
} // namespace unllm::algorithm
