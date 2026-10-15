#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "unllm/unllm.h"

namespace unllm {

bool Vectors::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t count = 0, dim = 0;
    if (!f.read(magic, 4) || std::memcmp(magic, "UNLV", 4) != 0) return false;
    f.read(reinterpret_cast<char*>(&count), 4);
    f.read(reinterpret_cast<char*>(&dim), 4);
    if (!f || count == 0 || dim == 0 || dim > 4096 || count > 2000000) return false;
    dim_ = dim;
    data_.resize(std::size_t(count) * dim);
    norms_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t len = 0;
        f.read(reinterpret_cast<char*>(&len), 1);
        std::string w(len, '\0');
        f.read(w.data(), len);
        float* row = data_.data() + std::size_t(i) * dim;
        f.read(reinterpret_cast<char*>(row), std::streamsize(dim) * 4);
        if (!f) {
            dim_ = 0;
            return false;
        }
        double n = 0;
        for (uint32_t d = 0; d < dim; ++d) n += double(row[d]) * row[d];
        norms_[i] = float(std::sqrt(n));
        words_[w] = i;
    }
    return true;
}

const float* Vectors::row(const std::string& w) const {
    auto it = words_.find(w);
    return it == words_.end() ? nullptr : data_.data() + it->second * dim_;
}

double Vectors::similarity(const std::string& a, const std::string& b) const {
    const float* ra = row(a);
    const float* rb = row(b);
    if (!ra || !rb) return 0;
    double na = norms_[words_.at(a)], nb = norms_[words_.at(b)];
    if (na == 0 || nb == 0) return 0;
    double dot = 0;
    for (std::size_t d = 0; d < dim_; ++d) dot += double(ra[d]) * rb[d];
    return dot / (na * nb);
}

}  // namespace unllm
