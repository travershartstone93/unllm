#include "search.h"
#include "session.h"
#include "unllm/symbolic/gui_target.h"
#include <iostream>
using namespace unllm::algorithm;
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      std::cout << "unllm --algorithm synthesize CONTRACT\n"
                   "unllm --algorithm construct SESSION CONTRACT PYTHON\n"
                   "unllm --algorithm revise SESSION CHANGES PYTHON\n"
                   "unllm --algorithm repair IR CONTRACT\n"
                   "unllm --algorithm check IR CONTRACT PYTHON\n"
                   "unllm --algorithm cpp|python IR\n"
                   "unllm --algorithm run IR JSON_ARGS\n";
      return 0;
    }
    if (argc == 5) {
      std::string mode = argv[1];
      auto read = [](const char *p) {
        return Json::parse(unllm::native::read_file(p, 262144));
      };
      if (mode == "construct" || mode == "revise") {
        auto r = construct(argv[2], read(argv[3]), mode == "revise", argv[4]);
        std::cout << r.dump() << "\n";
        return 0;
      }
      if (mode == "check") {
        auto dir = unllm::native::fresh_gui_directory(
            std::filesystem::temp_directory_path(), "unllm-search-check");
        try {
          auto r = check(read(argv[2]), read(argv[3]), dir, argv[4]);
          std::cout << r.dump() << "\n";
        } catch (...) {
          std::filesystem::remove_all(dir);
          throw;
        }
        std::filesystem::remove_all(dir);
        return 0;
      }
    }
    if (argc < 3 || argc > 4)
      throw std::runtime_error("See unllm --algorithm --help");
    if (argc == 3 && std::string_view(argv[1]) == "synthesize") {
      auto r =
          synthesize(Json::parse(unllm::native::read_file(argv[2], 262144)));
      std::cout << r.dump() << "\n";
      return 0;
    }
    if (argc == 4 && std::string_view(argv[1]) == "repair") {
      auto prior = Json::parse(unllm::native::read_file(argv[2], 65536));
      decode(prior);
      auto r = synthesize(
          Json::parse(unllm::native::read_file(argv[3], 262144)), prior);
      std::cout << r.dump() << "\n";
      return 0;
    }
    auto p = decode(Json::parse(unllm::native::read_file(argv[2], 65536)));
    std::string mode = argv[1];
    if (argc == 3 && (mode == "cpp" || mode == "python"))
      std::cout << emit(p, mode == "python");
    else if (argc == 4 && mode == "run") {
      auto args = Json::parse(argv[3]);
      const auto &f = p.functions.back();
      if (!args.is_array() || args.size() != f.parameters)
        throw std::runtime_error("argument count");
      std::vector<Value> values;
      for (size_t i = 0; i < args.size(); ++i)
        values.push_back(argument(args.at(i), f.slots[i]));
      std::cout << json(interpret(p, values)).dump() << '\n';
    } else
      throw std::runtime_error("Invalid mode or argument count");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
