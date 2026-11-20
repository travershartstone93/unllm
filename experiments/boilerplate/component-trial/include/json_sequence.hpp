#pragma once
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <memory>
#include <json-c/json.h>

namespace unllm {
// Serialize an ordered range of strings or signed integers as a JSON array.
template<class Range>
void write_json_sequence(const char* path, const Range& values) {
    std::unique_ptr<json_object, decltype(&json_object_put)> array(json_object_new_array(), json_object_put);
    if (!array) throw std::bad_alloc();
    for (const auto& value : values) {
        using T = std::decay_t<decltype(value)>;
        json_object* item;
        if constexpr (std::is_same_v<T, std::string>)
            item = json_object_new_string_len(value.data(), static_cast<int>(value.size()));
        else {
            static_assert(std::is_integral_v<T> && std::is_signed_v<T>);
            item = json_object_new_int64(value);
        }
        if (!item) throw std::bad_alloc();
        if (json_object_array_add(array.get(), item) != 0) {
            json_object_put(item);
            throw std::bad_alloc();
        }
    }
    std::ofstream out(path);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << json_object_to_json_string_ext(array.get(), JSON_C_TO_STRING_PLAIN);
    out.close();
}
}
