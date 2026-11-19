# N18 · Does a preference for shorter contracts fix selection?

For each distinct candidate in a fixed SEMPRE beam, subtract a fitted cost for each operation from the existing parse score. The development fit selected a cost of 4 and recovered six of eight earlier positive contracts at rank one. The original model's top candidates were reproduced before fitting. New calibration selected a required adjusted score gap of 1 and disabled the unpenalized fallback. These score units have no probabilistic interpretation. [Frozen design](protocol.md) · [Selection implementation](score.cpp)

| Separate transfer suite | Stock CLI | Unpenalized fallback plus stock | Adjusted fallback plus stock |
| --- | ---: | ---: | ---: |
| Correct programs on 8 positives, verified Python and C++ | 0 | 0 | 5 |
| Wrong acceptances on 8 positives and 6 controls | 0 | 0 | 0 |
| Supported requests declined | 8 | 8 | 3 |

The correct contract was present on five of the eight positive beams. The unpenalized winner was incorrect on all eight before abstention. The cost adjustment selected all five available correct contracts. Every accepted contract compiled or loaded in both languages and passed the empty and populated input cases. The normal CLI exhausted its reading budget on all 14 prompts in both languages. We passed selected typed contracts through the normal CLI from the experimental harness. No English fallback was installed. All eight supplied-contract controls passed both targets. [Per-request predictions](results.tsv) · [Execution evidence](executions.tsv) · [Stock replies](stock.json)

Two of the three misses used wider paraphrases and lacked expected candidates. The remaining miss asked for negative values in reverse order. The selected grammar assumes surface order matches operation order and supports two stream input types and a bounded catalog. Several test requests reuse phrases from training. These results support a narrow transfer claim and triggered the separately frozen [broad evaluation](../../broad-results.md).

Replay with the same compiled SEMPRE checkout and JDK as in the [parent study](../README.md). Run from this directory. The beam-dump script reuses the original parser and training setup and changes only the reporting loop.

```sh
root=$(git rev-parse --show-toplevel)
trial_dir=$(cat "$root/.state/n16-bridge.path")
replay=$(mktemp -d)
JAVAC=/usr/lib/jvm/java-17-openjdk/bin/javac JAVA=/usr/lib/jvm/java-17-openjdk/bin/java \
  ./dump-beams.sh "$trial_dir/tools/sempre-master" "$replay"
c++ -std=c++20 -O2 score.cpp -o "$replay/score"
"$replay/score" < "$replay/beams.tsv" | cmp - results.tsv
./check-programs.sh "$replay/programs" | cmp - executions.tsv
```
