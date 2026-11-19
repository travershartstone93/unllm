#pragma once
#include <array>
#include <string>

namespace trial {
// Each accepted expression has degree <= 4. Five distinct evaluations uniquely
// identify the polynomial over mathematical integers. Source supplies a prior
// only; every generated candidate still uses checked IR operations.
using Signature = std::array<int, 5>;
inline constexpr Signature variable{-2, -1, 0, 1, 2};
constexpr Signature constant(int n) {
  Signature s{};
  s.fill(n);
  return s;
}
static_assert(constant(7) == Signature{7, 7, 7, 7, 7});
inline Signature combine(char op, Signature a, Signature b) {
  for (size_t i = 0; i < a.size(); ++i)
    a[i] = op == '+' ? a[i] + b[i] : op == '-' ? a[i] - b[i] : a[i] * b[i];
  return a;
}
inline std::string key(Signature s) {
  std::string out;
  for (auto n : s)
    out += std::to_string(n) + ",";
  return out;
}
} // namespace trial
