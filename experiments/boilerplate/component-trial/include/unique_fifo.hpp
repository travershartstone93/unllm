#pragma once
#include <algorithm>
#include <list>
#include <optional>

namespace unllm {
template<class T>
class UniqueFifo {
    std::list<T> entries_;
public:
    bool push(const T& item) {
        if (std::find(entries_.begin(), entries_.end(), item) != entries_.end()) return false;
        entries_.push_back(item);
        return true;
    }
    bool cancel(const T& item) {
        auto it = std::find(entries_.begin(), entries_.end(), item);
        if (it == entries_.end()) return false;
        entries_.erase(it);
        return true;
    }
    std::optional<T> pop() {
        if (entries_.empty()) return {};
        T value = entries_.front();
        entries_.pop_front();
        return value;
    }
};
}
