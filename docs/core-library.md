# Closed-domain language library

ActivityScope uses the library in its Ask page and command to answer questions about a machine’s recorded history. The caller supplies program names, records and facts. The analyzer extracts a reading, the caller searches the relevant records, and the realizer turns the results into a sentence. A caller can use the domain score to refuse a question and explain which terms it recognized. [ActivityScope integration](https://github.com/travershartstone93/ActivityScope/blob/main/src/core/askcore.cpp) · [Public C++ interface](../include/unllm/unllm.h)

## Using the analyzer

After building the core library in the [quickstart](../README.md#quickstart-for-the-core-library), save this as `ask.cpp` in the repository root.

```cpp
#include <ctime>
#include <iostream>
#include <unllm/unllm.h>

int main() {
    unllm::Analyzer analyzer;
    analyzer.set_programs({"blender", "firefox"});
    auto reading = analyzer.read("Did Firefox run last night?", std::time(nullptr));
    for (const auto& entity : reading.entities)
        std::cout << entity.type << ' ' << entity.value << '\n';
}
```

```sh
c++ -std=c++20 -Iinclude ask.cpp build-core/libunllm.a -lsqlite3 -pthread -o ask
./ask
```

The example prints `program firefox`. The reading also carries the time window, intents and search terms. It can distinguish a live query such as “right now” from a past window, and a comparison such as “hotter than yesterday” from a request for the latest temperature. Follow-ups can change one part of an earlier reading, for example “and Firefox?” or “just the evening.” The caller chooses which measurements answer the question.

The analyzer recognizes domain vocabulary, approximate spellings, quantities and process IDs. Editable tables supply synonyms, conversational rewrites and fixed banter. A shipped table of static word vectors can help recognize otherwise unknown words. Record retrieval uses SQLite FTS5 and can restrict results to the understood time window. The realizer handles agreement, tense, negation, lists and pronoun references, so the caller can reuse the same facts for a count, a yes/no answer or a summary. [Vocabulary and tables](../data/) · [Realizer and analyzer examples](../tests/exam.cpp)

## Evaluation

The project authors wrote the questions and expected readings in the core exam. It fixes the reference time so that relative dates have known answers. The core-only build passed all 111 question readings and reported 513 checks with zero failures, including the supporting unit checks. The exam is a regression suite used during development. [Exam and assertions](../tests/exam.cpp)

The project curated a separate coverage corpus from generalized Ask Ubuntu, Super User and Unix & Linux questions, public WildChat and LMSYS chat turns, and autocomplete-style and conversational examples. On its 159 lines, the tool reported 120 understood, 26 banter matches, 10 ordinary refusals and 3 poor refusals. “Understood” means the analyzer reached its domain-score threshold. A poor refusal means it recognized the machine or a topic but still fell below that threshold. These classifications measure routing coverage. The corpus has no human-scored answer for each line. [Phrasings and sources](../data/corpus.txt) · [Scoring code](../tools/coverage.cpp)

Run from the repository root after the quickstart.

```sh
cmake --build build-core --target unllm_coverage --parallel
./build-core/unllm_coverage data/corpus.txt
```
