#pragma once

#include <string>
#include <unordered_set>

namespace unllm {
enum class LatchEvent { trigger, rearm, hold };

// Return true only for the first trigger for a key since construction or rearm.
class KeyedLatch {
public:
    bool observe(const std::string& key, LatchEvent event) {
        if (event == LatchEvent::trigger) return active_.insert(key).second;
        if (event == LatchEvent::rearm) active_.erase(key);
        return false;
    }

private:
    std::unordered_set<std::string> active_;
};
}
