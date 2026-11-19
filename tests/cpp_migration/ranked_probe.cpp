// Link once against the frozen search archive and once against the native
// backend to exercise the same ABI against the same fixture pack.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
extern "C" {
void *us_open(const char *);
void us_close(void *);
uint32_t us_schema(const void *);
int us_search2(const void *, const char *, const char *, const uint32_t *,
               const int64_t *, uint32_t, uint32_t, uint32_t *, int64_t *,
               int64_t *, float *);
void us_set_boosts(void *, float, float);
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  auto search = us_open(argv[1]);
  if (!search)
    return 1;
  std::printf("schema %u\n", us_schema(search));
  uint32_t selected_shard = 0;
  int64_t doc = 2;
  for (auto rule : {"n-1", "0", "0.67"}) {
    setenv("UNLLM_MIN_MATCH", rule, 1);
    for (auto weights : {0.f, 4.f}) {
      us_set_boosts(search, weights, .5);
      for (auto query :
           {"mountain rivers", "rivers river", "the yellow mountain",
            "foxes arctic snow", "café", "nonexistent"}) {
        for (int mode = 0; mode < 3; ++mode) {
          uint32_t shards[10];
          int64_t pids[10], docs[10];
          float scores[10];
          auto n = us_search2(search, query, mode == 1 ? "lead" : nullptr,
                              mode == 2 ? &selected_shard : nullptr,
                              mode == 2 ? &doc : nullptr, mode == 2, 10, shards,
                              pids, docs, scores);
          std::printf("%s|%.0f|%s|%d|%d", rule, weights, query, mode, n);
          for (int i = 0; i < n; ++i)
            std::printf("|%u,%lld,%lld,%.6f", shards[i], (long long)pids[i],
                        (long long)docs[i], scores[i]);
          std::puts("");
        }
      }
    }
  }
  us_close(search);
}
