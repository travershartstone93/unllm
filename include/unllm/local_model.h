#pragma once
// The reader: a small language model served by Ollama on this machine
// (qwen3:4b by default), asked to answer a question from the passages it is
// given and from nothing else, naming the passage it read. No network beyond
// localhost; when Ollama is not running, the caller falls back to quoting.
#include <optional>
#include <string>
#include <vector>

namespace unllm {

struct ModelAnswer {
    std::string answer;  // the model's short answer; empty when it says no passage states it
    int passage = 0;     // 1-based index of the passage it cites, 0 for none
};

class LocalModel {
public:
    // "off" disables the reader. Returns whether it is enabled.
    bool configure(const std::string& model, int port = 11434);
    bool enabled() const { return !model_.empty(); }
    const std::string& model() const { return model_; }
    // Nothing (not an answer) when Ollama is unreachable, times out or replies
    // with something unreadable.
    std::optional<ModelAnswer> ask(const std::string& question, const std::vector<std::string>& passages,
                                   int timeout_s = 120) const;

private:
    std::string model_;
    int port_ = 11434;
};

// A JSON string body (between the quotes) decoded: escapes and \uXXXX, surrogate pairs included.
std::string json_unescape(const std::string& s);
// The string value of "key" in a JSON text, or "" when absent; numbers come back as their digits.
std::string json_field(const std::string& text, const std::string& key);

}  // namespace unllm
