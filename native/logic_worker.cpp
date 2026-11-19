// One embedded SWI owner per resident worker. The parent enforces deadlines
// and reaps the entire process group if native inference does not return.
#include "unllm/json.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
extern "C" char *unllm_logic_request(const char *);
extern "C" void unllm_logic_free(char *);
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  const auto parent = getppid();
  if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parent)
    return 3;
  const auto home =
      (std::filesystem::path(argv[1]) / ".deps/native/usr/lib/swipl").string();
  const auto rules =
      std::filesystem::absolute(argv[1]) / "reasoning/prolog/rules.pl";
  if (setenv("UNLLM_NATIVE_RULE_PATH", rules.c_str(), 1) != 0 ||
      setenv("SWI_HOME_DIR", home.c_str(), 1) != 0)
    return 3;
  std::string line;
  char c;
  while (std::cin.get(c)) {
    if (c != '\n') {
      if (line.size() >= 16 * 1024 * 1024)
        return 4;
      line += c;
      continue;
    }
    std::unique_ptr<char, decltype(&unllm_logic_free)> result(
        unllm_logic_request(line.c_str()), unllm_logic_free);
    if (!result)
      return 5;
    std::cout << result.get() << '\n' << std::flush;
    if (!std::cout)
      return 6;
    line.clear();
  }
  return line.empty() ? 0 : 7;
}
