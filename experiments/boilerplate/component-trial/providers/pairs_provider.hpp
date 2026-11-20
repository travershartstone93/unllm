#pragma once
#include "provider_types.hpp"
#include "fifo_pairs.hpp"
#include <utility>

namespace brick_data {
inline std::vector<std::int64_t> unmatched_rows(const Rows& rows, std::size_t reference,
        std::size_t amount, std::size_t side, const std::string& first) {
    unllm::FifoPairs<std::pair<std::string, std::int64_t>> matcher;
    rows([&](std::int64_t row, const Fields& fields) {
        matcher.observe({fields.at(reference), std::stoll(fields.at(amount))}, fields.at(side) == first, row);
    });
    return {matcher.unmatched().begin(), matcher.unmatched().end()};
}
}
