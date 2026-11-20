// Independent client for the admitted input reader. The runner pre-includes its
// candidate header.
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
  const std::string first = "file:///tmp/A%20B",
                    second = "https://example.org/a#fragment";
  std::ofstream("in", std::ios::binary) << "# ignored\r\n"
                                        << first << "\r\n\r\n"
                                        << second << '\n'
                                        << first;
  if (UNLLM_TEST_BRICK("in") != std::vector<std::string>{first, second, first})
    return 1;
  std::ofstream("comments", std::ios::binary)
      << "# first\r\n\r\n# second\n" << first << "\n\n# last";
  if (UNLLM_TEST_BRICK("comments") != std::vector<std::string>{first})
    return 4;
  std::ofstream("empty");
  if (!UNLLM_TEST_BRICK("empty").empty())
    return 2;
  std::ofstream("nul", std::ios::binary).write("a\0b", 3);
  std::ofstream("bad", std::ios::binary).put(static_cast<char>(0xff));
  for (auto path : {"absent", "nul", "bad"}) {
    try {
      UNLLM_TEST_BRICK(path);
      return 3;
    } catch (const std::runtime_error &) {
    }
  }
}
