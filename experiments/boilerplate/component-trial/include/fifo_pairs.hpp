#pragma once
#include <array>
#include <deque>
#include <map>
#include <set>

namespace unllm {
// Caller supplies increasing unique row IDs, a comparison key and one of two sides.
template<class Key>
class FifoPairs {
    using Rows = std::array<std::deque<std::size_t>, 2>;
    std::map<Key, Rows> by_key_;
    std::set<std::size_t> pending_;
public:
    void observe(const Key& key, bool side, std::size_t row) {
        auto& queues = by_key_[key];
        auto& opposite = queues[!side];
        if (opposite.empty()) {
            queues[side].push_back(row);
            pending_.insert(row);
        } else {
            pending_.erase(opposite.front());
            opposite.pop_front();
            if (opposite.empty() && queues[side].empty()) by_key_.erase(key);
        }
    }
    const std::set<std::size_t>& unmatched() const { return pending_; }
};
}
