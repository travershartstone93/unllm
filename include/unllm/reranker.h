#pragma once
// The reranker: a cross-encoder (bge-reranker-v2-m3) that scores how well a
// passage answers a question, read through ONNX Runtime on the GPU when its
// CUDA provider loads and on the CPU otherwise. It orders the pool the word
// search and the meaning search found. Installed under
// ~/.local/share/unllm/models/reranker; rerank.json there names the files.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace unllm {

class Reranker {
public:
    Reranker();
    ~Reranker();
    Reranker(const Reranker&) = delete;
    Reranker& operator=(const Reranker&) = delete;
    bool load(const std::string& dir, int threads = 4);
    bool ready() const;
    bool on_gpu() const;
    // One score per document, higher is better. A document is "title: text".
    // Empty when the model could not run: the caller keeps its own order.
    std::vector<float> scores(const std::string& question, const std::vector<std::string>& docs) const;
    // The token ids the model sees for one pair, for checking against the
    // Python tokenizer.
    std::vector<int64_t> encode(const std::string& question, const std::string& doc) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace unllm
