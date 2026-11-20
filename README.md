# unllm

Deterministic language understanding for closed domains, plus experiments in verified program construction. Research in progress.

| Component | What it does | Needs | Status and evidence |
|---|---|---|---|
| [Core library](docs/core-library.md) | Reads questions about a known domain, identifies times, topics and entities, and turns facts into English answers. No model inference at runtime. | C++20, SQLite with FTS5, threads | Powers Ask in ActivityScope. Passed 111/111 project-authored exam questions. On 159 project-curated phrasings, the analyzer marked 75% understood, matched 16% to banter and classified 2% as poor refusals. [Exam and coverage protocol](docs/core-library.md#evaluation) |
| [Reference packs](docs/PACKS.md) | Searches local texts by words and embeddings, reranks passages, then quotes one or uses a local model to write a cited answer. | Downloaded packs and indexes, SQLite, ONNX Runtime and SentencePiece for reranking, optional Ollama reader | On 50 Haiku-authored, Opus-reviewed questions selected because Qwen missed them from memory, the resident pipeline matched 37 answer keys. The earlier rules matched 6. [Protocol and scores](docs/PACKS.md#reference-pack-evaluation) |
| [Symbolic engine](docs/UPGRADE.md) | Answers from asserted facts and constructs utilities and stateful programs from supported behavior contracts. | Native Linux build, ACE/ERG, SWI-Prolog, Soufflé and cvc5. GUI targets also need Qt. | Research runtime with [validated examples](examples/upgrade/README.md). |
| [Algorithm synthesis](experiments/algorithm_ir/SYNTHESIS.md) | Searches a bounded set of program operations against typed examples and emits C++ and Python. | Native C++ build (includes cvc5), C++ compiler, Python to run Python output. | Passed reserved cases on 10/12 tasks designed by the implementing assistant. Development validation. [Protocol and task families](experiments/algorithm_ir/SYNTHESIS.md#frozen-development-evaluation) |
| [C++ coding pipeline](experiments/boilerplate/integrated/README.md) | A hosted model interprets English. Local tools select reusable C++ blocks, assemble, compile and test. Remaining gaps can trigger a hosted edit. | OpenCode, UDPipe, BGE, native tools, external brick corpus and a task evaluator | Pilot with external assets. A direct-generation baseline exists for two user-requested converters. A matched baseline for the full fallback suite is still open. [Comparison](docs/EXPERIMENT_LOG.md#b17-direct-generation-of-the-two-converters) · [Local setup](docs/local-setup.md) |

## Quickstart for the core library

From the repository root on Linux, with CMake 3.20 or later, a C++20 compiler and SQLite development files installed, run the following commands.

```sh
cmake -S . -B build-core -DUNLLM_SYMBOLIC=OFF -DUNLLM_TANTIVY=OFF -DUNLLM_ONNX=OFF -DUNLLM_RERANKER=OFF
cmake --build build-core --target unllm_tests --parallel 4
ctest --test-dir build-core -R '^exam$' --output-on-failure
```

This builds the static library and runs its exam using data in the checkout. [C++ usage example](docs/core-library.md#using-the-analyzer)

## Limits

Applications supply domain vocabularies and facts. The core can refuse questions outside that domain. Reference-pack answers depend on what the installed sources cover. Program construction covers bounded tasks, and a generated program is checked against the supplied tests. Unrestricted natural-language programming and arbitrary repository changes are outside the demonstrated scope. [Coding roadmap](docs/CODING_CAPABILITY_ROADMAP.md) · [Experiment log](docs/EXPERIMENT_LOG.md) · [MIT license](LICENSE)
