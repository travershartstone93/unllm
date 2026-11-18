// Development endpoint until all migration gates pass; production is unchanged.
#include "unllm/symbolic/engine.h"
#include <iostream>
#include <unistd.h>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "Usage: unllm_symbolic_worker ROOT FACT_DB LOGIC_WORKER\n";
    return 2;
  }
  try {
    Engine engine(argv[2], argv[1], argv[3]);
    std::string line;
    char c;
    while (std::cin.get(c)) {
      if (c != '\n') {
        if (line.size() >= 65536) {
          std::cerr << "Request exceeds 64 KiB\n";
          return 3;
        }
        line += c;
        continue;
      }
      auto started = std::chrono::steady_clock::now();
      Json result;
      try {
        auto request = Json::parse(line);
        if (request["operation"].text() == "import")
          result = engine.store.import_document(request["document"]);
        else
          result = engine.ask(request, after_ms(3000));
      } catch (const Ambiguous &e) {
        result = Json::object();
        result.set("status", Json::string("ambiguous"));
        result.set("text", Json::string(e.what()));
      } catch (const Unsupported &e) {
        result = Json::object();
        result.set("status", Json::string("unsupported"));
        result.set("text", Json::string(e.what()));
      } catch (const ResourceLimit &e) {
        result = Json::object();
        result.set("status", Json::string("resource_limit"));
        result.set("text", Json::string(e.what()));
      } catch (const std::exception &e) {
        result = Json::object();
        result.set("status", Json::string("error"));
        result.set("text", Json::string(e.what()));
      }
      result.set("elapsed_ms",
                 Json::real(std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - started)
                                .count()));
      result.set("parser_ms", Json::real(engine.parser.last_ms));
      result.set("build", Json::string("unllm-native-development-1"));
      auto worker = Json::object();
      worker.set("pid", Json::number(getpid()));
      worker.set("rule_version", Json::string(engine.native.rule_version()));
      result.set("worker", worker);
      std::cout << result.dump() << '\n' << std::flush;
      if (!std::cout)
        return 4;
      line.clear();
    }
    return line.empty() ? 0 : 5;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
