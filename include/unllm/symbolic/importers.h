#pragma once
#include "store.h"
namespace unllm::native {
std::string title_key(const std::string &text);
Json find_documents(const std::filesystem::path &packs,
                    const std::vector<std::string> &titles,
                    const std::vector<std::string> &pack_names = {});
Json import_packs(FactStore &store, const std::filesystem::path &packs,
                  const std::vector<std::string> &titles,
                  const std::vector<std::string> &pack_names = {});
Json import_wikidata(FactStore &store, const std::vector<std::string> &qids,
                     const std::filesystem::path &cache, int depth = 1,
                     int max_entities = 50, bool fetch = true);
} // namespace unllm::native
