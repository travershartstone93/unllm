# unllm

Everything you wanted the LLM for, at a millionth of the compute, containing
no LLM at all.

unllm is a C++ library for understanding questions and writing answers in
closed domains, deterministically. An 8B language model spends on the order
of a trillion floating-point operations to answer a question about your own
machine's logs. unllm answers the same question in about ten microseconds of
table lookups, pattern rules and arithmetic, with no model file, no GPU, no
network, and the same answer every time.

Questions about the world are the exception. Those are answered from
reference packs built on this machine (docs/PACKS.md), and since October
2026 that path is a search pipeline, not rules: a word search and a meaning
search each hand over their best 25 passages, a cross-encoder reranker
(bge-reranker-v2-m3 through ONNX Runtime) orders the pool, and a small local
model (qwen3:4b through Ollama, when it is running) writes a short answer
from the top three and cites the passage it read. Without the model, the
top passage is quoted. Every answer still names its source; nothing is
invented. On questions a 4B model cannot answer from memory, this scores
about 37 of 50 against 6 for the old rules.

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
  network, sharing, updates, config, crashes, trust, battery, startup,
  devices, firmware flashing). Topics are ranked by evidence: "why was the
  disk busy" is a disk question that happens to say busy, not a busy
  question that happens to say disk.
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
  lists ("a, b and c"), negation ("did not crash"), yes/no answers ("No,
  Firefox did not crash"), aggregation (one subject's facts merge into one
  sentence) and unambiguous pronouns (a subject returning immediately is
  "it"; after an interruption its name is repeated).
- Question shape: polar questions ("did blender run yesterday") and counting
  questions ("how many times did firefox crash") are recognized as such, so
  the caller can answer yes or no, or with a number.
- Superlatives: "the hottest it has been", "which program used the most
  memory", caught before the stemmer folds "hottest" down to "hot".
- Live time: "right now", "currently", "at the moment" resolve to a marked
  live window, so the caller knows to measure instead of recall.
- Self-reference: a question that names the machine ("what is this machine")
  is recognized as being about the domain itself, routed to an identity
  topic, and never told it was not about the machine.
- Retrieval: a BM25 index over the caller's records (SQLite FTS5 with the
  porter stemmer), filtered by the understood time window.
- A synonym table (data/synonyms.txt, editable text) so words the topics
  never listed still land: "lethargic" reaches "slow".
- The meaning map (data/vectors.bin, 243 KB): static word vectors distilled
  once from a language model (tools/export_vectors.py, MIT model). Using it
  is lookups and arithmetic, never inference; it is the last resort when the
  exact layers recognize nothing, and the whole table ships with the
  library.
- Banter (data/banter.txt): fixed, palatable replies to the classics people
  type at anything with a text box. "What is the meaning of life" gets "42",
  not a cold refusal. The trigger families were mined from sixty years of
  chatbot logs (ELIZA, ALICE); the replies are this library's own.
- Rewrites (data/rewrites.txt): the chatty coat comes off before analysis,
  so "could you please tell me what crashed" reads exactly like "what
  crashed". Reductions are data; the classics stay out of their reach.
- The answer's shape: "when" wants a moment, "why" wants the cause first,
  "how long" wants a duration, "which program" wants a name. Carried on the
  reading so the caller answers in the right form.
- Conversation: a terse follow-up patches one dimension of the previous
  question ("and firefox?" swaps the program, "just the evening" moves the
  window, "how many times?" recounts), and "it" resolves to the one program
  under discussion, only when there is exactly one and the question is not
  about the machine itself.
- Judgment frames: "is that normal", "hotter than yesterday", "getting
  slower", "did it change after the update" are recognized as statistics
  questions, with the comparison referent carried separately, so the caller
  can answer from baselines instead of stories.
- A health-check intent: "what is wrong with this machine" routes to the
  caller's diagnosis rules instead of a story.
- Realizer growth: the present perfect ("has been up for two days"), the
  contrast join ("used 74% of the CPU, but did not crash") and shared
  predicates merging into one plural subject ("Firefox and chromium
  crashed").

Every capability is pinned by the exam in tests/exam.cpp: 96 questions
with hand-computed correct readings, a speed budget (under a millisecond per
question, measured about 25 microseconds), and unit checks, 254 in all. The
library grows one exam failure at a time. Breadth is measured separately:
data/corpus.txt holds real-world phrasings mined from top-voted support-site
questions and public chat datasets, and tools/coverage.cpp reports how much
of it is answerable (currently 75 percent understood plus 16 percent banter,
and under 3 percent refused badly). The exam is the gate; the corpus is the
gauge.

## Deployed

The ask command and Ask page in ActivityScope answer plain questions about
a machine from its recorded history through this library: time window,
topic and search in, grammatical sentences and receipts out, microseconds
per question, nothing leaving the machine.

## Use

    cmake -B build && cmake --build build
    ./build/unllm_tests

    #include <unllm/unllm.h>
    unllm::Analyzer an;
    an.set_programs({"blender", "firefox"});
    auto r = an.read("why did the fan scream last night?", time(nullptr));
    // r.window, r.intents, r.entities, r.terms

C++20, no dependencies. MIT.
