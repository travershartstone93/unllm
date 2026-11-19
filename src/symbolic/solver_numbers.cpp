#include "solver_numbers.h"
#include <gmp.h>
#include <mpfr.h>
namespace unllm::native {
std::string solver_decimal(const Json &value) {
  std::string text = canonical(value);
  auto pos = text.find_first_of("eE");
  if (pos == std::string::npos)
    return text;
  int exponent = std::stoi(text.substr(pos + 1));
  text.resize(pos);
  std::string sign;
  if (text.starts_with("-")) {
    sign = "-";
    text.erase(0, 1);
  }
  auto point = text.find('.');
  int n = int(point == std::string::npos ? text.size() : point) + exponent;
  if (point != std::string::npos)
    text.erase(point, 1);
  if (n <= 0)
    return sign + "0." + std::string(-n, '0') + text;
  if (size_t(n) >= text.size())
    return sign + text + std::string(n - text.size(), '0');
  return sign + text.insert(n, ".");
}
double solver_binary64(const std::string &rational) {
  if (rational.size() > 65536)
    throw ResourceLimit("Solver rational exceeds decoding budget");
  mpq_t exact;
  mpq_init(exact);
  if (mpq_set_str(exact, rational.c_str(), 10) != 0 ||
      mpz_sgn(mpq_denref(exact)) == 0) {
    mpq_clear(exact);
    throw Unsupported("Malformed solver rational");
  }
  mpq_canonicalize(exact);
  // Retain enough rational precision to resolve a binary64 rounding
  // boundary, including subnormal values, before rounding to the target.
  mpfr_t rounded;
  mpfr_init2(rounded, mpz_sizeinbase(mpq_denref(exact), 2) + 2200);
  mpfr_set_q(rounded, exact, MPFR_RNDN);
  double value = mpfr_get_d(rounded, MPFR_RNDN);
  mpfr_clear(rounded);
  mpq_clear(exact);
  return value;
}
} // namespace unllm::native
