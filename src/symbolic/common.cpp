#include "unllm/symbolic/common.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <openssl/evp.h>
#include <set>
#include <unicode/normalizer2.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>

namespace unllm::native {
std::string read_file(const std::filesystem::path &path, std::size_t limit) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("Cannot open: " + path.string());
  std::string result;
  std::array<char, 8192> buffer;
  while (in) {
    in.read(buffer.data(), buffer.size());
    result.append(buffer.data(), in.gcount());
    if (result.size() > limit)
      throw ResourceLimit("File size budget exceeded");
  }
  if (in.bad())
    throw std::runtime_error("Cannot read: " + path.string());
  return result;
}
std::string key(const std::string &text) {
  for (int32_t i = 0; i < static_cast<int32_t>(text.size());) {
    UChar32 cp;
    U8_NEXT(text.data(), i, static_cast<int32_t>(text.size()), cp);
    if (cp < 0)
      throw Unsupported("Invalid UTF-8");
  }
  UErrorCode error = U_ZERO_ERROR;
  const auto *normalizer = icu::Normalizer2::getNFKCInstance(error);
  icu::UnicodeString normalized;
  normalizer->normalize(icu::UnicodeString::fromUTF8(text), normalized, error);
  if (U_FAILURE(error))
    throw std::runtime_error("Unicode normalization failed");
  normalized.foldCase();
  icu::UnicodeString out;
  bool space = false;
  for (int32_t i = 0; i < normalized.length();) {
    UChar32 cp = normalized.char32At(i);
    i += U16_LENGTH(cp);
    if (u_isUWhiteSpace(cp) || (cp >= 0x1c && cp <= 0x1f)) {
      space = !out.isEmpty();
      continue;
    }
    if (space)
      out.append(UChar32(' '));
    space = false;
    out.append(cp);
  }
  std::string result;
  out.toUTF8String(result);
  return result;
}
static std::string float_text(double v) {
  if (!std::isfinite(v))
    throw Unsupported("Non-finite JSON value");
  std::array<char, 128> b;
  auto [end, ec] = std::to_chars(b.data(), b.data() + b.size(), v,
                                 std::chars_format::scientific);
  if (ec != std::errc())
    throw std::runtime_error("Number encoding failed");
  std::string s(b.data(), end);
  auto ep = s.find('e');
  int exponent = std::stoi(s.substr(ep + 1));
  if (exponent < -4 || exponent >= 16)
    return s;
  bool negative = s.front() == '-';
  std::string digits = s.substr(negative ? 1 : 0, ep - (negative ? 1 : 0));
  digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
  int point = exponent + 1;
  std::string out = negative ? "-" : "";
  if (point <= 0)
    out += "0." + std::string(-point, '0') + digits;
  else if (point >= static_cast<int>(digits.size()))
    out += digits + std::string(point - digits.size(), '0') + ".0";
  else
    out += digits.substr(0, point) + "." + digits.substr(point);
  return out;
}
std::string canonical(const Json &value) {
  if (value.is_object()) {
    auto members = value.members();
    std::sort(members.begin(), members.end(),
              [](auto &a, auto &b) { return a.first < b.first; });
    std::string out = "{";
    bool first = true;
    for (const auto &[k, v] : members) {
      if (!first)
        out += ",";
      first = false;
      out += canonical(Json::string(k)) + ":" + canonical(v);
    }
    return out + "}";
  }
  if (value.is_array()) {
    std::string out = "[";
    bool first = true;
    for (const auto &v : value.items()) {
      if (!first)
        out += ",";
      first = false;
      out += canonical(v);
    }
    return out + "]";
  }
  if (value.type() == json_type_double)
    return float_text(value.real());
  // json-c escapes slashes by default; Python's canonical encoding does not.
  if (value.is_string()) {
    std::string out = "\"";
    const char *hex = "0123456789abcdef";
    for (unsigned char c : value.text()) {
      switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 32) {
          out += "\\u00";
          out += hex[c >> 4];
          out += hex[c & 15];
        } else
          out += char(c);
      }
    }
    return out + "\"";
  }
  return value.dump();
}
std::string sha256(const std::string &value) {
  unsigned char bytes[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  if (!EVP_Digest(value.data(), value.size(), bytes, &size, EVP_sha256(),
                  nullptr))
    throw std::runtime_error("SHA256 failed");
  std::string out;
  const char *hex = "0123456789abcdef";
  for (unsigned i = 0; i < size; ++i) {
    out += hex[bytes[i] >> 4];
    out += hex[bytes[i] & 15];
  }
  return out;
}
std::string digest(const Json &value) { return sha256(canonical(value)); }
std::string required_text(const Json &value, const std::string &field) {
  auto v = value[field];
  if (!v.is_string() || v.text().empty())
    throw Unsupported("Expected nonempty string: " + field);
  return v.text();
}
void fields_only(const Json &value,
                 std::initializer_list<std::string> allowed) {
  if (!value.is_object())
    throw Unsupported("Expected an object");
  std::set<std::string> names(allowed);
  for (const auto &[k, v] : value.members())
    if (!names.contains(k))
      throw Unsupported("Unknown field: " + k);
}
} // namespace unllm::native
