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
    // A follow-up question made to stand on its own, given the last exchanges
    // (question and answer pairs, oldest first; two are enough for "the
    // difference between them"): "why is it so cold there" after Mongolia ->
    // "why is it so cold in Mongolia". A question that already stands on its
    // own comes back as it is. Nothing when the model is unreachable.
    std::optional<std::string> rewrite(const std::vector<std::pair<std::string, std::string>>& exchanges,
                                       const std::string& question, int timeout_s = 60) const;
    // Any prompt that asks for a JSON reply: the reply's JSON text, for the
    // caller to read with json_field. Nothing when the model is unreachable.
    std::optional<std::string> json(const std::string& prompt, int timeout_s = 60) const;

private:
    std::string model_;
    int port_ = 11434;
};

// A JSON string body (between the quotes) decoded: escapes and \uXXXX, surrogate pairs included.
std::string json_unescape(const std::string& s);
// The string value of "key" in a JSON text, or "" when absent; numbers come back as their digits.
std::string json_field(const std::string& text, const std::string& key);

}  // namespace unllm
