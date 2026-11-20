#pragma once
#include <array>
#include <fstream>
#include <stdexcept>
#include <zlib.h>

namespace unllm {
// Decode all RFC1952 members; reject truncated input, bad checksums and foreign bytes.
inline void expand_gzip(const char* input, const char* output) {
    std::ifstream in(input, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open compressed input");
    z_stream stream{};
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) throw std::runtime_error("inflateInit failed");
    struct Cleanup { z_stream& stream; ~Cleanup() { inflateEnd(&stream); } } cleanup{stream};
    std::ofstream out(output, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    std::array<unsigned char, 32768> raw, plain;
    bool member_done = false;
    for (;;) {
        if (!stream.avail_in) {
            in.read(reinterpret_cast<char*>(raw.data()), raw.size());
            if (in.bad()) throw std::runtime_error("Input read error");
            stream.avail_in = static_cast<uInt>(in.gcount());
            stream.next_in = raw.data();
            if (!stream.avail_in) {
                if (!member_done) throw std::runtime_error("Incomplete gzip member");
                break;
            }
        }
        if (member_done && inflateReset(&stream) != Z_OK) throw std::runtime_error("inflateReset failed");
        stream.next_out = plain.data();
        stream.avail_out = plain.size();
        int status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) throw std::runtime_error("Invalid gzip data");
        out.write(reinterpret_cast<const char*>(plain.data()), plain.size() - stream.avail_out);
        member_done = status == Z_STREAM_END;
    }
    out.close();
}
}
