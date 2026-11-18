#include "unllm/symbolic/engine.h"
#include "unllm/symbolic/linking.h"
#include <iostream>
#include <unistd.h>
using namespace unllm::native;
static void check(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  auto dir = std::filesystem::temp_directory_path() /
             ("unllm-chat-" + std::to_string(getpid()));
  try {
    std::filesystem::create_directory(dir);
    {
      auto linked = link_entities("Is New York in ＦＲＡＮＣＥ?",
                                  {"New", "New York", "France"});
      check(linked.text == "Is Zzunllmnamea in Zzunllmnameb?" &&
                linked.names.at("zzunllmnamea") == "New York",
            "longest Unicode dictionary link");
      check(link_entities("New, York", {"New York"}).names.empty(),
            "names cannot cross punctuation");
      Engine engine(dir / "facts.db", argv[1], argv[2]);
      auto ask = [&](const std::string &text,
                     const std::string &context = "default") {
        auto r = Json::object();
        r.set("question", Json::string(text));
        r.set("context", Json::string(context));
        auto result = engine.ask(r, after_ms(5000));
        return result;
      };
      check(ask("Paris is in France.")["truth"].text() == "asserted",
            "assertion");
      check(ask("Is Paris in France?")["truth"].text() == "supported",
            "question");
      check(ask("Is it in France?")["truth"].text() == "supported", "pronoun");
      check(ask(":why")["text"].text().find("Premises:") != std::string::npos,
            "explanation");
      check(ask("Paris is not in France.")["truth"].text() == "asserted",
            "explicit negative assertion");
      check(ask("Is Paris in France?")["truth"].text() == "both",
            "conflicting evidence");
      ask(":undo");
      check(ask("Is Paris in France?")["truth"].text() == "supported",
            "undo assertion");
      ask(":correct Paris is not in France.");
      check(ask("Is Paris in France?")["truth"].text() == "refuted",
            "correction");
      ask(":undo");
      check(ask("Is Paris in France?")["truth"].text() == "supported",
            "undo correction");
      bool unknown = false;
      try {
        ask("Is Paris in France?", "other");
      } catch (const Unsupported &) {
        unknown = true;
      }
      check(unknown, "conversation isolation");
      engine.store.import_document(Json::parse(
          R"({"source":{"id":"test:deps"},"entities":[{"id":"app","name":"Atlas","type":"program"},{"id":"lib","name":"Cedar","type":"program"}],"claims":[{"predicate":"depends_on","subject":"app","object":"lib"}]})"));
      auto blocked = ask("Is Atlas blocked?");
      check(blocked["truth"].text() == "unknown" &&
                blocked["missing_premises"].size() > 0,
            "missing evidence");
      ask("Cedar is available.");
      check(ask("Is Atlas blocked?")["truth"].text() == "unknown",
            "open world");
      ask(":complete depends_on Atlas");
      check(ask("Is Atlas blocked?")["truth"].text() == "refuted",
            "explicit completeness");
      ask(":correct Cedar is not available.");
      check(ask("Is Atlas blocked?")["truth"].text() == "supported",
            "changed premise");
      auto hypothetical = ask("What if Cedar is available?");
      check(hypothetical["hypothetical"].flag() &&
                hypothetical["truth"].text() == "refuted",
            "hypothetical override");
      check(ask("Is Atlas blocked?")["truth"].text() == "supported",
            "hypothesis leaves actual context intact");
      ask(":reset");
      check(ask("Is Atlas blocked?")["truth"].text() == "unknown",
            "reset conversation but preserve source facts");
    }
    std::filesystem::remove_all(dir);
    std::cout << "Native live conversation, correction/undo, scope and "
                 "isolation checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::filesystem::remove_all(dir);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
