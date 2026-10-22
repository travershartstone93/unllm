#include "unllm/reranker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef UNLLM_HAVE_RERANKER
#include <onnxruntime_cxx_api.h>
#include <sentencepiece_processor.h>
#endif

namespace unllm {

namespace {

// The value of "key" in a small JSON file, strings and numbers only.
std::string json_value(const std::string& text, const std::string& key) {
    auto at = text.find("\"" + key + "\"");
    if (at == std::string::npos) return "";
    at = text.find(':', at);
    if (at == std::string::npos) return "";
    ++at;
    while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
    if (at < text.size() && text[at] == '"') {
        std::string out;
        for (++at; at < text.size() && text[at] != '"'; ++at) {
            if (text[at] == '\\' && at + 1 < text.size()) ++at;
            out += text[at];
        }
        return out;
    }
    std::string out;
    while (at < text.size() && (std::isalnum(static_cast<unsigned char>(text[at])) || text[at] == '.' || text[at] == '-'))
        out += text[at++];
    return out;
}

}  // namespace

struct Reranker::Impl {
    int max_len = 512, batch = 8;
    int64_t cls = 0, pad = 1, sep = 2, unk = 3, offset = 1;
    bool gpu = false;
#ifdef UNLLM_HAVE_RERANKER
    sentencepiece::SentencePieceProcessor sp;
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "unllm-reranker"};
    std::unique_ptr<Ort::Session> session;
#endif
};

Reranker::Reranker() : impl_(std::make_unique<Impl>()) {}
Reranker::~Reranker() = default;

bool Reranker::load(const std::string& dir, int threads) {
#ifdef UNLLM_HAVE_RERANKER
    Impl& im = *impl_;
    std::ifstream jf(dir + "/rerank.json");
    if (!jf) return false;
    std::stringstream ss;
    ss << jf.rdbuf();
    const std::string j = ss.str();
    auto num = [&](const char* key, int64_t fallback) {
        const std::string v = json_value(j, key);
        return v.empty() ? fallback : int64_t(std::atoll(v.c_str()));
    };
    im.max_len = int(num("max_len", 512));
    im.batch = int(num("batch", 8));
    im.cls = num("cls", 0);
    im.pad = num("pad", 1);
    im.sep = num("sep", 2);
    im.unk = num("unk", 3);
    im.offset = num("offset", 1);
    const std::string file = json_value(j, "file");
    const std::string tokenizer = json_value(j, "tokenizer");
    if (file.empty() || tokenizer.empty()) return false;
    if (!im.sp.Load(dir + "/" + tokenizer).ok()) return false;
    // The GPU when ONNX Runtime's CUDA provider loads; UNLLM_RERANK_CPU=1 keeps it off.
    auto open = [&](bool cuda) {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(threads);
        so.SetInterOpNumThreads(1);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (cuda) {
            OrtCUDAProviderOptions o{};
            so.AppendExecutionProvider_CUDA(o);
        }
        return std::make_unique<Ort::Session>(im.env, (dir + "/" + file).c_str(), so);
    };
    const bool want_gpu = std::getenv("UNLLM_RERANK_CPU") == nullptr;
    try {
        im.session = open(want_gpu);
        im.gpu = want_gpu;
    } catch (const Ort::Exception&) {
        try {
            im.session = open(false);
            im.gpu = false;
        } catch (const Ort::Exception&) {
            im.session.reset();
            return false;
        }
    }
    return true;
#else
    (void)dir;
    (void)threads;
    return false;
#endif
}

bool Reranker::ready() const {
#ifdef UNLLM_HAVE_RERANKER
    return impl_->session != nullptr;
#else
    return false;
#endif
}

bool Reranker::on_gpu() const { return impl_->gpu; }

std::vector<int64_t> Reranker::encode(const std::string& question, const std::string& doc) const {
    std::vector<int64_t> ids;
#ifdef UNLLM_HAVE_RERANKER
    const Impl& im = *impl_;
    if (!ready()) return ids;
    // XLM-R's layout: <s> question </s></s> passage </s>; every SentencePiece id
    // moves up by the offset, and its unknown piece (0) becomes the model's.
    std::vector<int> q, d;
    im.sp.Encode(question, &q);
    im.sp.Encode(doc, &d);
    auto map = [&](int id) { return id == 0 ? im.unk : int64_t(id) + im.offset; };
    const std::size_t qmax = std::size_t(std::max(im.max_len - 4, 1));
    if (q.size() > qmax) q.resize(qmax);
    const std::size_t room = std::size_t(im.max_len) - 4 - q.size();  // the passage is what gets cut
    if (d.size() > room) d.resize(room);
    ids.reserve(q.size() + d.size() + 4);
    ids.push_back(im.cls);
    for (int id : q) ids.push_back(map(id));
    ids.push_back(im.sep);
    ids.push_back(im.sep);
    for (int id : d) ids.push_back(map(id));
    ids.push_back(im.sep);
#else
    (void)question;
    (void)doc;
#endif
    return ids;
}

std::vector<float> Reranker::scores(const std::string& question, const std::vector<std::string>& docs) const {
    std::vector<float> out;
#ifdef UNLLM_HAVE_RERANKER
    const Impl& im = *impl_;
    if (!ready() || docs.empty()) return out;
    std::vector<std::vector<int64_t>> enc;
    enc.reserve(docs.size());
    for (const auto& d : docs) enc.push_back(encode(question, d));
    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const char* in_names[] = {"input_ids", "attention_mask"};
        const char* out_names[] = {"logits"};
        const std::size_t batch = std::size_t(std::max(im.batch, 1));
        for (std::size_t at = 0; at < enc.size(); at += batch) {
            const std::size_t b = std::min(batch, enc.size() - at);
            std::size_t L = 0;
            for (std::size_t i = 0; i < b; ++i) L = std::max(L, enc[at + i].size());
            // Padded to the longest row of the batch, the padding masked out.
            std::vector<int64_t> ids(b * L, im.pad), mask(b * L, 0);
            for (std::size_t i = 0; i < b; ++i)
                for (std::size_t k = 0; k < enc[at + i].size(); ++k) {
                    ids[i * L + k] = enc[at + i][k];
                    mask[i * L + k] = 1;
                }
            const std::array<int64_t, 2> shape{int64_t(b), int64_t(L)};
            std::array<Ort::Value, 2> in{Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), shape.data(), 2),
                                         Ort::Value::CreateTensor<int64_t>(mem, mask.data(), mask.size(), shape.data(), 2)};
            auto res = im.session->Run(Ort::RunOptions{nullptr}, in_names, in.data(), 2, out_names, 1);
            const float* logits = res[0].GetTensorData<float>();  // (b, 1)
            for (std::size_t i = 0; i < b; ++i) out.push_back(logits[i]);
        }
    } catch (const Ort::Exception&) {
        return {};
    }
#else
    (void)question;
    (void)docs;
#endif
    return out;
}

}  // namespace unllm
