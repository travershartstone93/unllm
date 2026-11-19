// Discovery-only manual lowering probe. No English interpretation or learned
// clauses.
#include "unllm/symbolic/common.h"
#include <charconv>
#include <iostream>
using namespace unllm::native;
namespace {
double quantity(const std::string &s, bool magnitude) {
  auto start = s.starts_with('-') ? 1u : 0u;
  auto dot = s.find('.');
  auto end = dot == s.npos ? s.size() : dot;
  if ((magnitude && start) || start == end || s.size() > 16 ||
      s.substr(start, end - start).find_first_not_of("0123456789") != s.npos ||
      (dot != s.npos &&
       (dot + 1 == s.size() || s.size() - dot - 1 > 6 ||
        s.substr(dot + 1).find_first_not_of("0123456789") != s.npos)))
    throw Unsupported("Expected a bounded decimal literal");
  double n;
  auto [p, err] = std::from_chars(s.data(), s.data() + s.size(), n);
  if (err != std::errc{} || p != s.data() + s.size() || std::abs(n) > 1000000)
    throw Unsupported("Numeric envelope exceeded");
  return n;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 4)
      throw Unsupported("Expected initial, add/sub and magnitude");
    auto initial = quantity(argv[1], false);
    std::string op = argv[2], amount = argv[3];
    quantity(amount, true);
    if (op != "sub" && op != "add")
      throw Unsupported("Unknown change");
    auto spec = Json::parse(
        R"({"schema":2,"program":"application","title":"Counter","state":{"value":{"type":"number"}},"views":[{"id":"number_view","kind":"number_input","state":"value"}],"actions":[{"id":"change","label":"Change"}],"events":[],"checks":[]})");
    spec["state"]["value"].set("initial", Json::real(initial));
    spec["actions"].at(0).set(
        "ensure",
        Json::string("next.value == prev.value " +
                     std::string(op == "sub" ? "-" : "+") + " " + amount));
    std::cout << spec.dump() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
