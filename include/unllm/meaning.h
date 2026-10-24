// Meaning search: the question embedded by a small sentence model (bge-small
// through ONNX Runtime, on the CPU) and matched against a compressed index of
// passage vectors (IVF-PQ, exported from faiss by bench/export_index.py as a
// .unlpq file, read in place through mmap). Measured on 2026-10-19: handing
// this search's list to the unchanged composer took the library from 71 to
// 113 right of 400, because a passage holding the answer usually lacks most
// of the question's words, which no word search can bridge.
//
// Optional. Without ONNX Runtime at build time or without the files, load()
// returns false and the library searches by words as before.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace unllm {

class Meaning {
public:
    struct Hit {
        int shard_no;
        int64_t pid;
        float score;
    };
    struct ShardRef {
        int no;            // the shard number used in the index's ids (id = no << 32 | pid)
        std::string pack;  // pack name, e.g. wikipedia-simple
        std::string file;  // shard file, e.g. shard-000.sqlite
        int file_no;       // NNN of shard-NNN.sqlite
    };
    Meaning();
    ~Meaning();
    // index_path: the .unlpq file; model_dir holds model.onnx, vocab.txt and
    // embed.json (query prefix, max_len). Only inner-product indexes are read.
    bool load(const std::string& index_path, const std::string& model_dir, int threads = 4);
    // The embedding model alone, no index: embed() works, search() does not.
    // For matching texts against each other (ActivityScope's ask kinds).
    bool load_model(const std::string& model_dir, int threads = 4);
    bool ready() const;      // index and model loaded: search works
    bool can_embed() const;  // model loaded: embed works
    int dim() const;
    int64_t size() const;
    // The text's vector, unit length. query: a question looking for passages,
    // which gets the model's search prefix; false for a text compared with
    // other texts, which gets none.
    std::vector<float> embed(const std::string& text, bool query = true) const;
    // The k best passages by inner product, looking in the nprobe nearest lists.
    std::vector<Hit> search(const std::vector<float>& q, int k, int nprobe = 64) const;
    const std::vector<ShardRef>& shards() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace unllm
