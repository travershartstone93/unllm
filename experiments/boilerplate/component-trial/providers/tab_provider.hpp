#pragma once
#include "provider_types.hpp"
#include "tab_rows.hpp"

namespace brick_data {
inline Rows tab_records(const char* path) {
    return [path = std::string(path)](const Visitor& consume) {
        unllm::tab_rows(path.c_str(), consume);
    };
}
inline Fields column(const Rows& rows, std::size_t index) {
    Fields values;
    rows([&](std::int64_t, const Fields& fields) { values.push_back(fields.at(index)); });
    return values;
}
}
