#pragma once
// World knowledge: reference packs searched and answered from, by unllm's
// own retrieval and composition. Packs are built on the user's machine from
// downloaded sources (tools/unllm_ingest.py); unllm reads them read-only.

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "unllm/extract.h"
#include "unllm/unllm.h"

struct sqlite3;

namespace unllm {

struct PackInfo {
    std::string dir, name, kind, source, version, license, attribution;
};

// One passage of a reference document, with everything an answer needs to
// cite it.
struct Passage {
    std::string pack, kind, title, section, role, text, url, author, license, attribution;
    int64_t pid = 0, doc_id = 0;
    int ord = 0;
    int shard = -1;
    double bm25 = 0, late = 0, title_score = 0, score = 0;
    bool phrase = false;  // holds the question's subject word for word ("powerhouse of the cell")
    bool named = false;   // its document was found by name ("mozart": Wolfgang Amadeus Mozart)
};

// A document found by its title.
struct DocRef {
    int shard = -1;
    int64_t doc_id = 0;
    std::string title;
    bool exact = false;  // titled exactly like the name asked for
    int passages = 0;    // how much the document says: a longer article is the better-known subject
};

struct Query {
    std::vector<std::string> words;   // the question's content words, as typed (lowercase)
    std::string subject;              // "photosynthesis", "computer memory": the thing asked about
    std::set<std::string> kinds;      // pack kinds to search; empty means all
    int per_shard = 20;
    int total = 200;
    int deadline_ms = 150;
};

// A search over every installed pack. Each pack is one or more SQLite shards
// with FTS5 indexes; shards are searched in parallel under a deadline.
class Library {
public:
    Library() = default;
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    bool open(const std::string& packs_dir);
    bool empty() const { return shards_.empty(); }
    const std::vector<PackInfo>& packs() const { return packs_; }
    // Passages for a question: exact title matches first, then passages that
    // contain all of the question's words, then any of them.
    std::vector<Passage> candidates(const Query& q) const;
    // The opening passages of documents titled exactly like subject.
    std::vector<Passage> by_title(const std::string& subject, const std::set<std::string>& kinds, int k = 3) const;
    // The passages after p in the same document.
    std::vector<Passage> neighbors(const Passage& p, int count) const;
    // Documents whose title contains every word of name: the one titled
    // exactly like name first, then the longest articles ("apple" finds
    // "Apple", then "Apple Inc.").
    std::vector<DocRef> titled(const std::string& name, const std::set<std::string>& kinds, int k) const;
    // A document's fact box, in order, as (key, value). A sub-row ("• total")
    // carries the row it belongs to ("Population total").
    std::vector<std::pair<std::string, std::string>> fact_box(int shard, int64_t doc_id) const;
    // The opening passage of a document.
    Passage lead_of(int shard, int64_t doc_id) const;
    // Plain ranked search with Tantivy: passages holding any of the words,
    // best first (bm25), across the packs of those kinds that have a Tantivy
    // index; role "lead" keeps opening passages only. Empty when unllm was
    // built without Tantivy or no pack has an index.
    std::vector<Passage> ranked(const std::string& words, const std::set<std::string>& kinds, int k,
                                const std::string& role = "") const;
    bool has_ranked() const;
    // The same search kept inside the given articles (DocRef shard and
    // doc_id), for finding the right passage of the top articles. On an
    // index built before schema 2 it filters a wider plain search instead.
    std::vector<Passage> ranked_in(const std::string& words, const std::vector<DocRef>& docs, int k,
                                   const std::string& role = "") const;
    // Weight of a word found in the article title and in the section
    // heading, against 1.0 in the passage text (defaults 1.0 and 0.5).
    // Set once after open, not while searches run.
    void set_ranked_boosts(float title, float section);
    // Lowest index schema across packs with an index: 2 when every index
    // holds titles and sections, 1 when one is older, 0 when none.
    int ranked_schema() const;
    // Opening passages that contain one of the phrases word for word
    // ("highest mountain") and a word from every group in need ({"world",
    // "earth"}), best matches first, each with how many passages its article
    // has. Shards are searched in parallel; what is not found by the
    // deadline is left out.
    std::vector<std::pair<Passage, int>> leads_with(const std::vector<std::string>& phrases,
                                                    const std::vector<std::vector<std::string>>& need,
                                                    const std::set<std::string>& kinds, int per_shard,
                                                    bool leads_only = true, int deadline_ms = 250) const;
    int64_t passage_count() const { return passages_; }
    // Passages by place, in the given order: pack name, shard file number
    // (NNN of shard-NNN.sqlite), pid; the score is kept as the word score.
    struct Place {
        std::string pack;
        int shard_no;
        int64_t pid;
        float score;
    };
    std::vector<Passage> by_place(const std::vector<Place>& places) const;
    // How many paragraphs an article has (shard index as in Passage::shard).
    int doc_size(int shard, int64_t doc_id) const;

private:
    struct Shard {
        sqlite3* db = nullptr;
        std::size_t pack = 0;
        int number = 0;  // NNN of shard-NNN.sqlite
    };
    std::vector<PackInfo> packs_;
    std::vector<Shard> shards_;
    std::vector<void*> engines_;  // one Tantivy index per pack, or null
    void read_hits(std::size_t pi, int n, const uint32_t* shard_nums, const int64_t* pids, const float* scores,
                   std::vector<Passage>& out) const;
    int64_t passages_ = 0;
};

// The key a document title is indexed under: lowercase letters and digits,
// single spaces, a leading "the", "a" or "an" dropped. Matches the ingest tool.
std::string title_key(const std::string& title);

// An answer from reference knowledge. Never invented: every sentence is a
// sentence of a cited passage; weak matches say so.
struct WorldAnswer {
    enum Kind { None, Answer, Closest, Definition, HowTo, Poem } kind = None;
    std::string text;
    std::vector<std::string> receipts;  // source title, URL and license per passage used
    std::vector<Passage> used;
    double confidence = 0;
    bool by_model = false;  // the reader model wrote it from the cited passage
};

class Reader;

class Meaning;
class Reranker;
class LocalModel;
class World {
public:
    // vectors: the meaning map for reranking (the large table when present).
    World(const Library& lib, const Vectors* vectors) : lib_(lib), vec_(vectors) {}
    // Answer a reading the router sent to the world. question is the text
    // the reading came from, for its words.
    WorldAnswer answer(const Reading& r, const std::string& question) const;
    // The same, also handing back the best passages the search ranked, in
    // order (empty when a fact box or a superlative answered directly).
    WorldAnswer answer(const Reading& r, const std::string& question, std::vector<Passage>* best) const;
    // A dictionary answer for one word or name, or None.
    WorldAnswer define(const std::string& word) const;
    // The next passages of the document an answer came from ("tell me more").
    WorldAnswer more(const Passage& p) const;
    // Which fact box rows answer which questions (data/factkeys.txt). Without
    // it, answers come from passages only.
    bool load_fact_keys(const std::string& path);
    // The short-answer extractor's tables (data/answertypes.txt,
    // data/reformulations.txt, data/units.txt); without them it stays off.
    bool load_extractor(const std::string& types, const std::string& reformulations, const std::string& units);
    // The reader model (unllm/reader.h), kept by the caller; null or not
    // ready, answers come from unllm's own rules only.
    void set_reader(const Reader* reader) { reader_ = reader; }
    // Meaning search (a vector index and the embedding model), when installed.
    void set_meaning(const Meaning* meaning) { meaning_ = meaning; }
    // The pipeline (2026-10-21): with a reranker installed, a world question
    // goes search -> reranker -> reader, and none of the rule layers run.
    // Word search and meaning search each give their top 25, the reranker
    // orders the pool, the reader answers from the top 3 and cites one; with
    // no reader, or none that answers, the top passage is quoted.
    void set_reranker(const Reranker* reranker) { reranker_ = reranker; }
    void set_model(const LocalModel* model) { model_ = model; }

private:
    // The pipeline's passages: the pooled top 25s in the reranker's order, 10 at most.
    std::vector<Passage> pipeline_passages(const Reading& r, const Query& q, const std::string& question) const;
    const Reranker* reranker_ = nullptr;
    const LocalModel* model_ = nullptr;
    // "what is the currency of japan", "who founded microsoft": the row of the
    // subject's fact box that answers it, or None.
    WorldAnswer fact_answer(const Reading& r, const std::string& question) const;
    // "what is the tallest mountain in the world", "who was the first person
    // in space": the article whose opening line says so of its own subject.
    WorldAnswer superlative_answer(const Reading& r, const std::string& question) const;
    // resolve: allow the superlative step (off when it has already named the subject).
    WorldAnswer answer(const Reading& r, const std::string& question, bool resolve,
                       std::vector<Passage>* best = nullptr) const;
    std::map<std::string, std::vector<std::string>> fact_keys_;
    AnswerTypes types_;
    Reformulations reform_;
    bool extract_ready_ = false;
    // A short answer voted on across the ranked passages; *ran says whether
    // the question had a shape the extractor handles at all.
    WorldAnswer extract(const Reading& r, const std::string& question, const std::vector<Passage>& ps,
                        bool* ran) const;
    // The reader's short answer over the ranked passages; *ran says whether
    // it read any.
    WorldAnswer read_answer(const std::string& question, const std::vector<Passage>& ps, bool* ran) const;
    const Reader* reader_ = nullptr;
    const Meaning* meaning_ = nullptr;
    // meaning: the question asked what a word MEANS (dictionary first).
    void rerank(const Reading& r, const std::vector<std::string>& words, std::vector<Passage>& ps,
                bool meaning) const;
    const Library& lib_;
    const Vectors* vec_;
};

// Sentences of a passage, split at full stops that end sentences (not
// "U.S.", "e.g." or initials).
std::vector<std::string> sentences_of(const std::string& text);

}  // namespace unllm
