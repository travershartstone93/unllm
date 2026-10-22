#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "unllm/unllm.h"

namespace unllm {

Vectors::~Vectors() {
    if (map_) munmap(const_cast<char*>(map_), map_size_);
}

bool Vectors::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4];
    uint32_t count = 0, dim = 0;
    if (!f.read(magic, 4)) return false;
    f.read(reinterpret_cast<char*>(&count), 4);
    f.read(reinterpret_cast<char*>(&dim), 4);
    if (!f || count == 0 || dim == 0 || dim > 4096 || count > 4000000) return false;

    if (std::memcmp(magic, "UNL2", 4) == 0) {
        // Read in place: the table can be large, and the page cache shares it
        // between every program that opens it.
        f.close();
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat sb {};
        if (fstat(fd, &sb) != 0) {
            ::close(fd);
            return false;
        }
        std::size_t need = 12 + std::size_t(count + 1) * 4 + std::size_t(count) * 4 + std::size_t(count) * dim;
        if (std::size_t(sb.st_size) < need) {
            ::close(fd);
            return false;
        }
        void* m = mmap(nullptr, std::size_t(sb.st_size), PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (m == MAP_FAILED) return false;
        map_ = static_cast<const char*>(m);
        map_size_ = std::size_t(sb.st_size);
        count_ = count;
        dim_ = dim;
        offsets_ = reinterpret_cast<const uint32_t*>(map_ + 12);
        scales_ = reinterpret_cast<const float*>(map_ + 12 + std::size_t(count + 1) * 4);
        rows_ = reinterpret_cast<const int8_t*>(map_ + 12 + std::size_t(count + 1) * 4 + std::size_t(count) * 4);
        blob_ = map_ + need;
        if (need + offsets_[count] > map_size_) {
            munmap(const_cast<char*>(map_), map_size_);
            map_ = nullptr;
            dim_ = 0;
            return false;
        }
        return true;
    }

    if (std::memcmp(magic, "UNLV", 4) != 0 || count > 2000000) return false;
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

long Vectors::find(const std::string& w) const {
    std::size_t lo = 0, hi = count_;
    while (lo < hi) {
        std::size_t mid = (lo + hi) / 2;
        const char* s = blob_ + offsets_[mid];
        std::size_t len = offsets_[mid + 1] - offsets_[mid];
        int c = std::memcmp(s, w.data(), std::min(len, w.size()));
        if (c == 0) c = len < w.size() ? -1 : len > w.size() ? 1 : 0;
        if (c == 0) return long(mid);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

bool Vectors::contains(const std::string& w) const {
    return map_ ? find(w) >= 0 : words_.count(w) > 0;
}

const float* Vectors::row(const std::string& w) const {
    auto it = words_.find(w);
    return it == words_.end() ? nullptr : data_.data() + it->second * dim_;
}

double Vectors::similarity(const std::string& a, const std::string& b) const {
    if (map_) {
        long ia = find(a), ib = find(b);
        if (ia < 0 || ib < 0) return 0;
        const int8_t* ra = rows_ + std::size_t(ia) * dim_;
        const int8_t* rb = rows_ + std::size_t(ib) * dim_;
        int32_t dot = 0;
        for (std::size_t d = 0; d < dim_; ++d) dot += int32_t(ra[d]) * int32_t(rb[d]);
        return double(dot) * double(scales_[ia]) * double(scales_[ib]);
    }
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
