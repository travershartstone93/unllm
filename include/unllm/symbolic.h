#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace unllm {
struct SymbolicReply {
    std::string text;
    std::string json;
    std::string status;
};

// One persistent local worker per Session. The worker owns semantic parsing,
// structured knowledge and the embedded native Prolog/Souffle adapter.
class Symbolic {
public:
    Symbolic();
    ~Symbolic();
    Symbolic(const Symbolic&) = delete;
    Symbolic& operator=(const Symbolic&) = delete;
    SymbolicReply ask(const std::string& question, int64_t now,
                      const std::string& context = "default");
    static bool enabled();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
