#pragma once
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace unllm {
using Prerequisites = std::map<std::string, std::set<std::string>>;
inline std::vector<std::vector<std::string>> dependency_waves(const Prerequisites& graph) {
    std::map<std::string, std::size_t> remaining;
    std::map<std::string, std::vector<std::string>> dependents;
    std::set<std::string> ready;
    for (const auto& [node, deps] : graph) {
        remaining[node] = deps.size();
        if (deps.empty()) ready.insert(node);
        for (const auto& dep : deps) {
            if (!graph.count(dep)) throw std::invalid_argument("Unknown prerequisite");
            dependents[dep].push_back(node);
        }
    }
    std::size_t count = 0;
    std::vector<std::vector<std::string>> waves;
    while (!ready.empty()) {
        waves.emplace_back(ready.begin(), ready.end());
        ready.clear();
        count += waves.back().size();
        for (const auto& done : waves.back())
            for (const auto& next : dependents[done])
                if (--remaining.at(next) == 0) ready.insert(next);
    }
    if (count != graph.size()) throw std::invalid_argument("Dependency cycle");
    return waves;
}
}
