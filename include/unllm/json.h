#pragma once
// Ownership wrapper around json-c; JSON parsing is delegated to the library.
#include <json-c/json.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

namespace unllm::json {
class Value {
public:
    Value() = default;
    explicit Value(json_object* p) : p_(p) {}
    Value(const Value& v) : p_(json_object_get(v.p_)) {}
    Value(Value&& v) noexcept : p_(std::exchange(v.p_, nullptr)) {}
    Value& operator=(Value v) noexcept { std::swap(p_, v.p_); return *this; }
    ~Value() { if (p_) json_object_put(p_); }
    static Value object() { return Value(json_object_new_object()); }
    static Value array() { return Value(json_object_new_array()); }
    static Value string(const std::string& s) {
        return Value(json_object_new_string_len(s.data(), int(s.size())));
    }
    static Value number(int64_t n) { return Value(json_object_new_int64(n)); }
    static Value real(double n) {
        if (!std::isfinite(n)) throw std::runtime_error("JSON numbers must be finite");
        return Value(json_object_new_double(n));
    }
    static Value boolean(bool b) { return Value(json_object_new_boolean(b)); }
    static Value parse(const std::string& s) {
        // A legal 16-level component derivation contains nested node objects,
        // argument arrays and child lists. json-c's default 32 levels cannot
        // carry those replies. Retain a finite bound above the planner limit.
        auto* tok = json_tokener_new_ex(128);
        json_tokener_set_flags(tok, JSON_TOKENER_STRICT);
        const std::string input = s + " "; // terminate scalar JSON tokens too
        auto* obj = json_tokener_parse_ex(tok, input.c_str(), int(input.size()));
        const auto err = json_tokener_get_error(tok);
        const std::string reason = json_tokener_error_desc(err);
        json_tokener_free(tok);
        if (err != json_tokener_success) {
            if (obj) json_object_put(obj);
            throw std::runtime_error("Invalid JSON: " + reason);
        }
        return Value(obj);
    }
    bool null() const { return !p_ || json_object_is_type(p_, json_type_null); }
    json_type type() const { return p_ ? json_object_get_type(p_) : json_type_null; }
    bool is_object() const { return type() == json_type_object; }
    bool is_string() const { return type() == json_type_string; }
    bool is_boolean() const { return type() == json_type_boolean; }
    bool is_integer() const { return type() == json_type_int; }
    bool is_number() const { return is_integer() || type() == json_type_double; }
    bool contains(const std::string& key) const {
        json_object* value = nullptr;
        return is_object() && json_object_object_get_ex(p_, key.c_str(), &value);
    }
    double real() const {
        if (!is_number()) throw std::runtime_error("Expected a JSON number");
        return json_object_get_double(p_);
    }
    Value clone() const { return parse(dump()); }
    Value at(std::size_t index) const {
        if (!is_array() || index >= json_object_array_length(p_)) throw std::out_of_range("JSON array index");
        return Value(json_object_get(json_object_array_get_idx(p_, index)));
    }
    std::size_t size() const {
        return is_array() ? json_object_array_length(p_) : is_object() ? json_object_object_length(p_) : 0;
    }
    void erase(const std::string& key) {
        if (!is_object()) throw std::runtime_error("Expected a JSON object");
        json_object_object_del(p_, key.c_str());
    }
    bool is_array() const { return p_ && json_object_is_type(p_, json_type_array); }
    std::string text(const std::string& fallback = "") const {
        if (!p_ || !json_object_is_type(p_, json_type_string)) return fallback;
        return std::string(json_object_get_string(p_), json_object_get_string_len(p_));
    }
    int64_t integer(int64_t fallback = 0) const {
        return p_ ? json_object_get_int64(p_) : fallback;
    }
    bool flag(bool fallback = false) const {
        return p_ ? json_object_get_boolean(p_) : fallback;
    }
    Value operator[](const std::string& key) const {
        json_object* value = nullptr;
        if (is_object() && json_object_object_get_ex(p_, key.c_str(), &value)) return Value(json_object_get(value));
        return {};
    }
    void set(const std::string& key, const Value& value) {
        if (!is_object()) throw std::runtime_error("Expected a JSON object");
        json_object_object_add(p_, key.c_str(), json_object_get(value.p_));
    }
    void push(const Value& value) { json_object_array_add(p_, json_object_get(value.p_)); }
    std::vector<Value> items() const {
        std::vector<Value> values;
        if (is_array()) {
            const auto count = json_object_array_length(p_);
            values.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
                values.emplace_back(json_object_get(json_object_array_get_idx(p_, i)));
        }
        return values;
    }
    std::vector<std::pair<std::string,Value>> members() const {
        std::vector<std::pair<std::string,Value>> values;
        if(p_ && json_object_is_type(p_,json_type_object)) {
            json_object_object_foreach(p_,name,value) {
                values.emplace_back(name,Value(json_object_get(value)));
            }
        }
        return values;
    }
    std::string dump() const {
        return p_ ? json_object_to_json_string_ext(p_, JSON_C_TO_STRING_PLAIN) : "null";
    }
private:
    json_object* p_ = nullptr;
};
}
