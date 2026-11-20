#pragma once
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace unllm {
// A plain TSV reader, with one-based row numbers and trailing empty fields preserved.
template<class Consumer>
void tab_rows(const char* path, Consumer consume) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open TSV");
    std::string line;
    std::size_t row = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (;;) {
            auto end = line.find('\t', start);
            fields.push_back(line.substr(start, end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        consume(++row, fields);
    }
    if (in.bad()) throw std::runtime_error("TSV read failed");
}
}
