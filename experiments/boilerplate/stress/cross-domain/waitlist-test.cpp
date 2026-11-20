#include "waitlist.hpp"
#include <cassert>
#include <optional>
#include <string>
int main() {
    Waitlist list, separate;
    assert(!list.take() && !list.cancel("Ann"));
    assert(list.join("Ann") && list.join("Bo") && !list.join("Ann"));
    assert(list.cancel("Ann") && !list.cancel("Ann") && list.join("Ann"));
    assert(list.take() == std::optional<std::string>("Bo"));
    assert(list.join("Bo") && list.take() == std::optional<std::string>("Ann"));
    assert(list.take() == std::optional<std::string>("Bo") && !list.take());
    assert(!separate.take());
}
