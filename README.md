# unllm

Everything you wanted the LLM for, at a millionth of the compute, containing
no LLM at all.

unllm is a C++ library for understanding questions and writing answers in
closed domains, deterministically. An 8B language model spends on the order
of a trillion floating-point operations to answer a question about your own
machine's logs. unllm answers the same question in about ten microseconds of
table lookups, pattern rules and arithmetic, with no model file, no GPU, no
network, and the same answer every time.

The deterministic language stack (intent parsing, slot filling, gazetteers,
surface realization) was state of the art until about 2019, then abandoned
for business reasons, not technical ones, when the industry moved to large
models. unllm revives it for the cases where an LLM is overkill: machine
assistants, log interrogation, command frontends, anywhere the vocabulary of
questions and the universe of answers are knowable in advance.

## What works today

- Tokenizer, light lemmatizer, typo-tolerant matching.
- Natural-language time: "yesterday evening", "between 9 and 11 last
  night", "two hours ago", "since monday", "last tuesday", "october 10th".
- Topic detection over editable vocabularies (heat, slow, memory, disk,
  network, sharing, updates, config, crashes, trust, battery, startup).
- Domain gazetteers: hand it your entity names, it finds them in questions,
  misspelled included. Quantities (90 C, 80%, 2 GB) and pids recognized.
- Follow-up memory: "and the day before?" shifts the previous window.
- A domain score: how much of the question was actually understood, so a
  caller can refuse to guess at off-topic questions instead of answering
  them confidently. Refusals are built in and never silent: an unanswerable
  question always gets "I can't answer that", with what little was
  recognized named.
- The realizer: grammatical English from raw facts, by rule. Agreement
  ("three programs were"), tense ("grow" to "grew"), plurals, number words,
  lists ("a, b and c"), aggregation (one subject's facts merge into one
  sentence) and unambiguous pronouns (a subject returning immediately is
  "it"; after an interruption its name is repeated).

Every capability is pinned by the exam in tests/exam.cpp: fifty questions
with hand-computed correct readings, a speed budget (under a millisecond per
question, measured about 12 microseconds), and unit checks. The library
grows one exam failure at a time.

- Retrieval: a BM25 index over the caller's records (SQLite FTS5 with the
  porter stemmer), filtered by the understood time window.
- A synonym table (data/synonyms.txt, editable text) so words the topics
  never listed still land: "lethargic" reaches "slow".
- The meaning map (data/vectors.bin, 243 KB): static word vectors distilled
  once from a language model (tools/export_vectors.py, MIT model). Using it
  is lookups and arithmetic, never inference; it is the last resort when the
  exact layers recognize nothing, and the whole table ships with the
  library.

## Planned

- The ask command: all of this pointed at a real machine's history.

## Use

    cmake -B build && cmake --build build
    ./build/unllm_tests

    #include <unllm/unllm.h>
    unllm::Analyzer an;
    an.set_programs({"blender", "firefox"});
    auto r = an.read("why did the fan scream last night?", time(nullptr));
    // r.window, r.intents, r.entities, r.terms

C++20, no dependencies. MIT.
