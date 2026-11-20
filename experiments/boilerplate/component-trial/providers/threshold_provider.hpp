#pragma once
#include "provider_types.hpp"
#include "keyed_latch.hpp"
#include <type_traits>

namespace brick_data {
template<bool emit_key, class Trigger, class Reset>
auto threshold_entries(const Rows& rows, std::size_t key, std::size_t value,
                       Trigger trigger, std::int64_t limit, Reset reset, std::int64_t recovery) {
    unllm::KeyedLatch latch;
    std::vector<std::conditional_t<emit_key, std::string, std::int64_t>> result;
    rows([&](std::int64_t row, const Fields& fields) {
        const auto& id = fields.at(key);
        auto n = std::stoll(fields.at(value));
        auto event = trigger(n, limit) ? unllm::LatchEvent::trigger :
                     reset(n, recovery) ? unllm::LatchEvent::rearm : unllm::LatchEvent::hold;
        if (latch.observe(id, event)) {
            if constexpr (emit_key) result.push_back(id);
            else result.push_back(row);
        }
    });
    return result;
}
}
