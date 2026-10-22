#include "unllm/reader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>

#ifdef UNLLM_HAVE_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace unllm {

namespace {

// One code point of the cleaned text and the bytes it came from.
struct Cp {
    char32_t c;
    std::size_t start, end;
};

std::vector<Cp> decode(const std::string& s) {
    std::vector<Cp> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        std::size_t n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
        if (i + n > s.size()) n = 1;
        char32_t c = n == 1 ? b : n == 2 ? b & 0x1F : n == 3 ? b & 0x0F : b & 0x07;
        for (std::size_t k = 1; k < n; ++k) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back({c, i, i + n});
        i += n;
    }
    return out;
}

void encode(char32_t c, std::string& out) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

bool is_space(char32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xA0 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x202F || c == 0x205F || c == 0x3000;
}

bool is_control(char32_t c) {
    return c == 0 || c == 0xFFFD || c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0) ||
           (c >= 0x200B && c <= 0x200F) || c == 0xFEFF || c == 0xAD;
}

bool is_punct(char32_t c) {
    if ((c >= 33 && c <= 47) || (c >= 58 && c <= 64) || (c >= 91 && c <= 96) || (c >= 123 && c <= 126)) return true;
    switch (c) {
        case 0xA1: case 0xA7: case 0xAB: case 0xB6: case 0xB7: case 0xBB: case 0xBF: case 0x37E: case 0x387:
            return true;
        default:
            break;
    }
    return (c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x205E) || (c >= 0x3001 && c <= 0x3003) ||
           (c >= 0x3008 && c <= 0x3011) || (c >= 0x27E6 && c <= 0x27EF) || (c >= 0x2983 && c <= 0x2998) ||
           (c >= 0x2E00 && c <= 0x2E4F) || c == 0x30FB || (c >= 0xFF1A && c <= 0xFF20) || (c >= 0x3014 && c <= 0x301F) || (c >= 0xFF01 && c <= 0xFF0F);
}

bool is_cjk(char32_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x20000 && c <= 0x2A6DF) ||
           (c >= 0x2A700 && c <= 0x2CEAF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0x2F800 && c <= 0x2FA1F);
}

// Lower case without accents, as the uncased model was trained ("É" -> "e").
// Returns 0 for a mark that only carries an accent.
char32_t fold(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0x80) return c;
    if (c >= 0x300 && c <= 0x36F) return 0;
    if (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7 && c != 0xDF) {  // ß stays
        if (c <= 0xDE) c += 32;
        static const char* base = "aaaaaa.ceeeeiiii.nooooo..uuuuy.y";  // 0xE0..0xFF
        const char b = base[c - 0xE0];
        return b == '.' ? c : char32_t(b);
    }
    if (c >= 0x100 && c <= 0x17F) {
        static const char* base =
            "aaaaaaccccccccdd..eeeeeeeeeegggggggghh..iiiiiiiii...jjkk.llllll....nnnnnn...oooooo..rrrrrrssssssss"
            "tttt..uuuuuuuuuuuuwwyyyzzzzzz.";
        const char b = base[c - 0x100];
        if (b != '.') return char32_t(b);
        if (c == 0x178) return 'y';
        static const char32_t up[] = {0x110, 0x126, 0x132, 0x13F, 0x141, 0x14A, 0x152, 0x166};
        for (char32_t u : up)
            if (c == u) return c + 1;
        return c;
    }
    switch (c) {  // Greek letters with an accent
        case 0x386: case 0x3AC: return 0x3B1;
        case 0x388: case 0x3AD: return 0x3B5;
        case 0x389: case 0x3AE: return 0x3B7;
        case 0x38A: case 0x3AF: case 0x3AA: case 0x3CA: case 0x390: return 0x3B9;
        case 0x38C: case 0x3CC: return 0x3BF;
        case 0x38E: case 0x3CD: case 0x3AB: case 0x3CB: case 0x3B0: return 0x3C5;
        case 0x38F: case 0x3CE: return 0x3C9;
        default: break;
    }
    switch (c) {  // Cyrillic letters with a mark (й -> и, ё -> е)
        case 0x419: case 0x439: case 0x40D: case 0x45D: return 0x438;
        case 0x401: case 0x451: case 0x400: case 0x450: return 0x435;
        case 0x407: case 0x457: return 0x456;
        case 0x40E: case 0x45E: return 0x443;
        case 0x403: case 0x453: return 0x433;
        case 0x40C: case 0x45C: return 0x43A;
        default: break;
    }
    // Voiced kana lose their dakuten mark (ガ -> カ).
    if ((c >= 0x3041 && c <= 0x3096) || (c >= 0x30A1 && c <= 0x30FA)) {
        const char32_t k = c >= 0x30A1 ? 0x60 : 0;
        const char32_t h = c - k;
        if (h >= 0x304C && h <= 0x3062 && (h % 2) == 0) return c - 1;
        if (h == 0x3065 || h == 0x3067 || h == 0x3069) return c - 1;
        if (h >= 0x3070 && h <= 0x307D && (h - 0x306F) % 3 != 0) return 0x306F + 3 * ((h - 0x306F) / 3) + k;
        if (h == 0x3094) return 0x3046 + k;
        if (c >= 0x30F7 && c <= 0x30FA) return c - 8;
        return c;
    }
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

}  // namespace

bool WordPiece::load(const std::string& vocab_path) {
    std::ifstream f(vocab_path);
    if (!f) return false;
    ids_.clear();
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ids_.emplace(line, n++);
    }
    auto get = [&](const char* p, int d) {
        int v = id(p);
        return v < 0 ? d : v;
    };
    cls_ = get("[CLS]", 101);
    sep_ = get("[SEP]", 102);
    pad_ = get("[PAD]", 0);
    unk_ = get("[UNK]", 100);
    return !ids_.empty();
}

int WordPiece::id(const std::string& piece) const {
    auto it = ids_.find(piece);
    return it == ids_.end() ? -1 : it->second;
}

std::vector<WordPiece::Piece> WordPiece::tokenize(const std::string& text) const {
    std::vector<Piece> out;
    std::vector<Cp> word;
    auto flush = [&] {
        if (word.empty()) return;
        if (word.size() > 100) {
            out.push_back({unk_, word.front().start, word.back().end});
            word.clear();
            return;
        }
        std::vector<Piece> pieces;
        std::size_t at = 0;
        bool bad = false;
        while (at < word.size()) {
            std::size_t end = word.size();
            int found = -1;
            while (end > at) {
                std::string s = at > 0 ? "##" : "";
                for (std::size_t k = at; k < end; ++k) encode(word[k].c, s);
                found = id(s);
                if (found >= 0) break;
                --end;
            }
            if (found < 0) {
                bad = true;
                break;
            }
            pieces.push_back({found, word[at].start, word[end - 1].end});
            at = end;
        }
        if (bad)
            out.push_back({unk_, word.front().start, word.back().end});
        else
            out.insert(out.end(), pieces.begin(), pieces.end());
        word.clear();
    };
    for (const Cp& raw : decode(text)) {
        if (is_space(raw.c)) {
            flush();
            continue;
        }
        if (is_control(raw.c)) continue;
        const char32_t c = fold(raw.c);
        if (c == 0) {  // an accent mark: its bytes join the letter before it
            if (!word.empty()) word.back().end = raw.end;
            continue;
        }
        if (is_punct(c) || is_cjk(c)) {
            flush();
            word.push_back({c, raw.start, raw.end});
            flush();
            continue;
        }
        word.push_back({c, raw.start, raw.end});
    }
    flush();
    return out;
}

struct Reader::Impl {
    WordPiece wp;
#ifdef UNLLM_HAVE_ONNX
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "unllm-reader"};
    std::unique_ptr<Ort::Session> session;
#endif
};

Reader::Reader() : impl_(std::make_unique<Impl>()) {}
Reader::~Reader() = default;

bool Reader::load(const std::string& model_dir, int threads) {
#ifdef UNLLM_HAVE_ONNX
    if (!impl_->wp.load(model_dir + "/vocab.txt")) return false;
    try {
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(threads);
        so.SetInterOpNumThreads(1);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl_->session = std::make_unique<Ort::Session>(impl_->env, (model_dir + "/model.onnx").c_str(), so);
    } catch (const Ort::Exception&) {
        impl_->session.reset();
        return false;
    }
    return true;
#else
    (void)model_dir;
    (void)threads;
    return false;
#endif
}

bool Reader::ready() const {
#ifdef UNLLM_HAVE_ONNX
    return impl_->session != nullptr;
#else
    return false;
#endif
}

std::vector<ReaderSpan> Reader::read(const std::string& question, const std::vector<std::string>& passages) const {
    std::vector<ReaderSpan> out;
#ifdef UNLLM_HAVE_ONNX
    if (!ready() || passages.empty()) return out;
    constexpr std::size_t kMax = 256, kQuestion = 64, kLongest = 30;
    const WordPiece& wp = impl_->wp;
    std::vector<WordPiece::Piece> q = wp.tokenize(question);
    if (q.size() > kQuestion) q.resize(kQuestion);
    // [CLS] question [SEP] passage [SEP], the passage cut to fit.
    struct Row {
        std::vector<WordPiece::Piece> ctx;
    };
    std::vector<Row> rows;
    std::vector<std::size_t> which;
    const std::size_t room = kMax - q.size() - 3;
    std::size_t len = 0;
    for (std::size_t i = 0; i < passages.size(); ++i) {
        auto ctx = wp.tokenize(passages[i]);
        if (ctx.empty()) continue;
        if (ctx.size() > room) ctx.resize(room);
        len = std::max(len, q.size() + ctx.size() + 3);
        rows.push_back({std::move(ctx)});
        which.push_back(i);
    }
    if (rows.empty()) return out;
    const std::size_t B = rows.size(), lo = q.size() + 2;
    std::vector<int64_t> ids(B * len, wp.pad()), mask(B * len, 0), types(B * len, 0);
    for (std::size_t b = 0; b < B; ++b) {
        int64_t* id = &ids[b * len];
        std::size_t t = 0;
        id[t++] = wp.cls();
        for (const auto& p : q) id[t++] = p.id;
        id[t++] = wp.sep();
        for (const auto& p : rows[b].ctx) {
            types[b * len + t] = 1;
            id[t++] = p.id;
        }
        types[b * len + t] = 1;
        id[t++] = wp.sep();
        std::fill(mask.begin() + long(b * len), mask.begin() + long(b * len + t), 1);
    }
    std::vector<float> st, en;
    try {
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 2> shape{int64_t(B), int64_t(len)};
        std::array<Ort::Value, 3> in{
            Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), shape.data(), 2),
            Ort::Value::CreateTensor<int64_t>(mem, mask.data(), mask.size(), shape.data(), 2),
            Ort::Value::CreateTensor<int64_t>(mem, types.data(), types.size(), shape.data(), 2)};
        const char* in_names[] = {"input_ids", "attention_mask", "token_type_ids"};
        const char* out_names[] = {"start_logits", "end_logits"};
        auto res = impl_->session->Run(Ort::RunOptions{nullptr}, in_names, in.data(), 3, out_names, 2);
        const float* s = res[0].GetTensorData<float>();
        const float* e = res[1].GetTensorData<float>();
        st.assign(s, s + B * len);
        en.assign(e, e + B * len);
    } catch (const Ort::Exception&) {
        return out;
    }
    for (std::size_t b = 0; b < B; ++b) {
        const float* s = &st[b * len];
        const float* e = &en[b * len];
        const std::size_t n = rows[b].ctx.size(), hi = lo + n;  // context is [lo, hi)
        std::vector<std::size_t> order(n);
        std::iota(order.begin(), order.end(), lo);
        const std::size_t top = std::min<std::size_t>(20, n);
        auto best_of = [&](const float* v) {
            std::vector<std::size_t> o = order;
            std::partial_sort(o.begin(), o.begin() + long(top), o.end(),
                              [&](std::size_t x, std::size_t y) { return v[x] > v[y]; });
            o.resize(top);
            return o;
        };
        const auto starts = best_of(s), ends = best_of(e);
        float best = -1e30f;
        std::size_t ba = lo, bb = lo;
        for (std::size_t a : starts)
            for (std::size_t z : ends)
                if (z >= a && z < a + kLongest && s[a] + e[z] > best) {
                    best = s[a] + e[z];
                    ba = a;
                    bb = z;
                }
        // Softmax over the context only, as the prototype measured.
        auto peak = [&](const float* v) {
            float m = -1e30f;
            for (std::size_t i = lo; i < hi; ++i) m = std::max(m, v[i]);
            double sum = 0;
            for (std::size_t i = lo; i < hi; ++i) sum += std::exp(double(v[i] - m));
            return 1.0 / sum;
        };
        ReaderSpan r;
        r.passage = which[b];
        r.start = rows[b].ctx[ba - lo].start;
        r.end = rows[b].ctx[bb - lo].end;
        r.text = passages[r.passage].substr(r.start, r.end - r.start);
        r.score = best;
        r.null_score = s[0] + e[0];
        r.prob = float(peak(s) * peak(e));
        out.push_back(std::move(r));
    }
#else
    (void)question;
    (void)passages;
#endif
    return out;
}

}  // namespace unllm
