#include "unllm/ranked_index.h"
#include <iostream>
#include <xapian.h>
namespace fs = std::filesystem;
int main(int argc, char **argv) {
  try {
    if (argc == 2) {
      unllm::build_ranked_index(argv[1]);
      return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--migrate") {
      for (auto &entry : fs::directory_iterator(argv[2])) {
        const auto &pack = entry.path();
        auto name = pack.filename().string();
        if (name.starts_with('.') || name.ends_with(".building") ||
            name.ends_with(".previous") ||
            !fs::is_regular_file(pack / "tantivy/meta.json") ||
            fs::exists(pack / "ranked"))
          continue;
        std::cout << "Migrating " << pack << std::endl;
        unllm::build_ranked_index(pack);
      }
      return 0;
    }
    std::cerr << "Usage: unllm_index PACK_DIR | --migrate PACKS_ROOT\n";
    return 2;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  } catch (const Xapian::Error &e) {
    std::cerr << e.get_description() << '\n';
    return 1;
  }
}
