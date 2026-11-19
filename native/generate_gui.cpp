// Isolated development command, not the production CLI cutover.
#include "unllm/symbolic/behaviour_job.h"
#include <iostream>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc < 6 || argc > 8) {
    std::cerr
        << "Usage: unllm_generate_gui ROOT LOGIC_WORKER SPEC_JSON LANGUAGE "
           "ARTIFACT_DIR [--emit-only | --python-target EXECUTABLE]\n";
    return 2;
  }
  try {
    bool emit_only = false;
    std::string python;
    if (argc == 7 && std::string(argv[6]) == "--emit-only")
      emit_only = true;
    else if (argc == 8 && std::string(argv[6]) == "--python-target")
      python = argv[7];
    else if (argc > 6)
      throw Unsupported("Unknown generation option");
    auto program = BehaviourSpec::from(Json::parse(read_file(argv[3], 65536)));
    NativeEngine native(argv[1], argv[2]);
    auto result = build_behaviour(program, argv[4], argv[1], argv[5], native,
                                  after_ms(180000), python, emit_only);
    std::cout << result.dump() << '\n';
    return result["status"].text() == "failed" ? 1 : 0;
  } catch (const std::exception &e) {
    auto out = Json::object();
    out.set("status", Json::string("error"));
    out.set("text", Json::string(e.what()));
    std::cout << out.dump() << '\n';
    return 1;
  }
}
