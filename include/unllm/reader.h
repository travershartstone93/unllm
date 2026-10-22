// The reader: a small trained model (MiniLM fine-tuned on SQuAD 2.0, run on
// the CPU with ONNX Runtime) that points at the words of a passage that
// answer a question, or says the passage holds no answer.
//
// It is optional. Without ONNX Runtime at build time, or without the model
// files, load() returns false and unllm answers as it did before.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace unllm {

// BERT's WordPiece tokenizer for an uncased vocabulary (vocab.txt, one piece
// per line, the line number is the id). Each piece keeps the byte range of
// the source text it came from, so answers are cut from the original text.
class WordPiece {
public:
    struct Piece {
        int id;
        std::size_t start, end;  // bytes in the source text
    };
    bool load(const std::string& vocab_path);
    bool ready() const { return !ids_.empty(); }
    std::vector<Piece> tokenize(const std::string& text) const;
    int id(const std::string& piece) const;  // -1 when not in the vocabulary
    int cls() const { return cls_; }
    int sep() const { return sep_; }
    int pad() const { return pad_; }
    int unk() const { return unk_; }

private:
    std::unordered_map<std::string, int> ids_;
    int cls_ = 101, sep_ = 102, pad_ = 0, unk_ = 100;
};

struct ReaderSpan {
    std::size_t passage = 0;  // index into the passages given
    std::size_t start = 0, end = 0;  // bytes in that passage
    std::string text;
    float score = 0;       // start + end logit of the span
    float null_score = 0;  // the same for "no answer here"
    float prob = 0;        // the span's start and end probabilities, multiplied
};

class Reader {
public:
    Reader();
    ~Reader();
    // model_dir holds model.onnx and vocab.txt.
    bool load(const std::string& model_dir, int threads = 4);
    bool ready() const;
    // The best span of each passage, in passage order (passages that are
    // empty after the question are left out).
    std::vector<ReaderSpan> read(const std::string& question, const std::vector<std::string>& passages) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace unllm
