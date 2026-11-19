// Resident development probe. Each line names a stage and returns its measured
// time plus a semantic check value. Initialization is outside the warm timer.
#include "unllm/symbolic/emit.h"
#include "unllm/symbolic/grounding.h"
#include <iostream>
using namespace unllm::native;
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    std::filesystem::path root = argv[1],
                          fixtures = root / "tests/cpp_migration/fixtures";
    auto v = Json::parse(read_file(fixtures / "semantics.json")).at(0);
    auto raw = v["mrs"].text(), sentence = v["sentence"].text();
    auto resolve = [](const std::string &name, bool) {
      return "entity:" + key(name);
    };
    auto parsed = decode_mrs(raw);
    SemanticParser parser(root);
    auto app = parser.parse("Make a standard calculator app.", resolve, {},
                            after_ms(11500), true);
    auto spec =
        Json::parse(read_file(fixtures / "programs.json"))["programs"].at(
            0)["input"];
    auto program = Program::from(spec);
    NativeEngine native(root, argv[2]);
    for (std::string stage; std::getline(std::cin, stage);) {
      auto begin = std::chrono::steady_clock::now();
      std::string value;
      if (stage == "decode")
        value = std::to_string(decode_mrs(raw).rels.size());
      else if (stage == "translate")
        value = translate(parsed, sentence, resolve).act;
      else if (stage == "live_parse")
        value = parser.parse(sentence, resolve).act;
      else if (stage == "ground")
        value = canonical(ground_program(app, root));
      else if (stage == "schema")
        value = canonical(Program::from(spec).data());
      else if (stage == "plan")
        value = canonical(plan(program, native, after_ms(5000))["components"]);
      else if (stage == "solve")
        value = synthesize_expression(*program.steps[1].expr).evidence.null()
                    ? "missing"
                    : "ok";
      else if (stage == "emit")
        value = emit_stream(program, "cpp", root).empty() ? "missing" : "ok";
      else
        return 2;
      auto elapsed = std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - begin)
                         .count();
      auto row = Json::object();
      row.set("ms", Json::real(elapsed));
      row.set("value", Json::string(value));
      std::cout << row.dump() << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
