#include "unllm/symbolic/store.h"
#include <iostream>
#include <unistd.h>
using namespace unllm::native;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "Expected rejection");
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  auto dir = std::filesystem::temp_directory_path() /
             ("unllm-store-" + std::to_string(getpid()));
  try {
    std::filesystem::create_directory(dir);
    {
      FactStore store(dir / "facts.db", argv[1]);
      auto initial = store.revision();
      auto document = Json::parse(
          R"({"schema":1,"source":{"id":"test:one","title":"Source one"},"entities":[{"id":"p","name":"Paris","type":"city","aliases":["ＰＡＲＩＳ"]},{"id":"f","name":"France","type":"country"}],"claims":[{"id":"located","predicate":"located_in","subject":"p","object":"f","valid_from":10,"valid_to":20}],"complete":[{"predicate":"located_in","subject":"p"}]})");
      auto imported = store.import_document(document);
      check(imported["claims"].integer() == 3, "type metadata and fact import");
      check(store.revision() == initial + 1, "revision");
      check(store.resolve("  ｐａｒｉｓ ").at(0)["id"].text() == "p",
            "normalized alias resolution");
      auto active = [&](int now) {
        for (auto &c : store.claims(now))
          if (c.id == "located")
            return true;
        return false;
      };
      check(!active(9) && active(10) && active(19) && !active(20),
            "half-open validity");
      check(store.completeness().size() == 1, "completeness");
      auto bad = document.clone();
      bad["claims"].at(0).set("negative", Json{});
      rejects([&] { store.import_document(bad); });
      check(store.revision() == initial + 1 && active(10),
            "failed validation leaves snapshot intact");
      bad = document.clone();
      bad["entities"].at(0).set("aliases", Json::parse("[42]"));
      rejects([&] { store.import_document(bad); });
      check(store.revision() == initial + 1 && active(10),
            "failed write transaction rolls back");
      rejects([&] {
        store.save_prepared("bad", Json::parse("{\"status\":\"timeout\"}"));
      });
      auto snapshot =
          Json::parse("{\"status\":\"complete\",\"tuples\":[[\"p\",\"f\"]]}");
      store.save_prepared("first", snapshot);
      check(canonical(store.prepared("first")) == canonical(snapshot),
            "prepared cache roundtrip");
      sqlite3 *db = nullptr;
      check(sqlite3_open((dir / "facts.db").c_str(), &db) == SQLITE_OK,
            "open corruption probe");
      check(sqlite3_exec(db, "UPDATE prepared SET checksum='bad'", nullptr,
                         nullptr, nullptr) == SQLITE_OK,
            "corrupt checksum");
      sqlite3_close(db);
      check(store.prepared("first").null(), "corrupt cache rejected");
      FactStore writer(dir / "facts.db", argv[1]);
      store.begin_read();
      check(store.revision() == initial + 1, "begin snapshot");
      auto changed = document.clone();
      changed.set("claims", Json::array());
      changed.set("complete", Json::array());
      writer.import_document(changed);
      check(active(10) && store.revision() == initial + 1,
            "read transaction isolated from source replacement");
      store.end_read();
      check(!active(10) && store.completeness().size() == 0 &&
                store.revision() == initial + 2,
            "replacement visible after read transaction");
      auto vectors =
          Json::parse(read_file(std::filesystem::path(argv[1]) /
                                "tests/cpp_migration/fixtures/store.json"));
      FactStore parity(dir / "parity.db", argv[1]);
      for (auto item : vectors.items()) {
        auto result = parity.import_document(item["document"]);
        check(canonical(result) == canonical(item["result"]),
              "reference import ID/digest parity");
        auto claims = Json::array();
        for (auto c : parity.claims(1234567890))
          claims.push(c.data());
        check(canonical(claims) == canonical(item["claims"]),
              "reference claims parity");
      }
    }
    std::filesystem::remove_all(dir);
    std::cout << "Store transaction, validity, cache and frozen reference "
                 "checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::filesystem::remove_all(dir);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
