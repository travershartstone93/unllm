#pragma once
#include <glib.h>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
namespace unllm_generated {
inline std::vector<std::string> read_g_uri_list_extract_uris(const char* path) {
    gchar* raw = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(path, &raw, &length, nullptr))
        throw std::runtime_error("Cannot read input file");
    std::unique_ptr<gchar, decltype(&g_free)> contents(raw, g_free);
    if (std::memchr(raw, 0, length) || !g_utf8_validate(raw, -1, nullptr))
        throw std::runtime_error("Expected UTF-8 text without embedded NUL");
    std::vector<std::string> result;
    auto append = [&](const char* text) {
        std::unique_ptr<gchar*, decltype(&g_strfreev)> values(g_uri_list_extract_uris(text), g_strfreev);
        if (values)
            for (auto p = values.get(); *p; ++p) result.emplace_back(*p);
    };
    std::istringstream input{std::string(raw, length)};
    for (std::string line; std::getline(input, line);) append(line.c_str());
    return result;
}
}

