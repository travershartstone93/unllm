#pragma once
// unllm: everything you wanted the LLM for, at a millionth of the compute,
// containing no LLM at all. Deterministic question understanding and answer
// writing for closed domains.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace unllm {

// ---- the pipeline's view of text ----

struct Token {
    std::string text;   // as typed
    std::string lower;  // lowercased
    std::string norm;   // lemma/stem: "crashes" -> "crash", "ran" -> "run"
    std::size_t start = 0;  // byte offset in the original question
};

std::vector<Token> tokenize(const std::string& text);
std::string normalize_word(const std::string& lower);  // lemma table + light stemmer

// ---- what a question is asking ----

enum class Intent {
    Heat, Slow, Memory, Disk, Network, Sharing, Update, Config, Crash,
    Trust, Battery, Startup, Story,  // Story: no specific topic, tell everything
};
const char* intent_name(Intent i);

struct Entity {
    std::string type;   // "program", "quantity", "pid"
    std::string value;  // canonical form: "blender", "90 C", "2671"
};

struct TimeWindow {
    int64_t from = 0, to = 0;  // unix seconds, half-open [from, to)
    bool stated = false;       // false: the question named no time, default applied
};

struct Reading {
    TimeWindow window;
    std::vector<Intent> intents;    // empty means Story
    std::vector<Entity> entities;
    std::vector<std::string> terms;  // normalized content words, for retrieval
    // How much of the question the library actually understood: matched
    // meaningful words over all meaningful words. Near 1: on home ground.
    // Near 0: this question is not about the domain; say so, do not guess.
    double domain = 0;
};

// ---- time phrases ----

// Finds a time expression in the tokens and resolves it against `now`
// (injected, so tests are stable). Consumed token indices are reported so
// they do not pollute the search terms.
std::optional<TimeWindow> find_time(const std::vector<Token>& tokens, int64_t now,
                                    std::set<std::size_t>* consumed = nullptr);

// ---- the analyzer ----

// ---- the meaning map: static word vectors, lookups and arithmetic ----
// A table distilled once from a language model; using it involves no model:
// a sentence's meaning is the mean of its word rows, similarity is a dot
// product. File format: "UNLV", u32 count, u32 dim, then per word a u8
// length, the word's bytes, and dim float32 coordinates.

class Vectors {
public:
    bool load(const std::string& path);
    bool ok() const { return dim_ > 0; }
    std::size_t size() const { return words_.size(); }
    // Cosine similarity in [-1, 1]; 0 when either word is unknown.
    double similarity(const std::string& a, const std::string& b) const;

private:
    const float* row(const std::string& w) const;
    std::map<std::string, std::size_t> words_;
    std::vector<float> data_;
    std::vector<float> norms_;
    std::size_t dim_ = 0;
};

// "canonical: word word word" lines; '#' comments. Returns word -> canonical.
std::map<std::string, std::string> load_synonyms(const std::string& path);

class Analyzer {
public:
    // The gazetteer of things that exist in the caller's domain.
    void set_programs(const std::vector<std::string>& names);
    // Words that mean other words: "lethargic" -> "slow". See load_synonyms.
    void set_synonyms(std::map<std::string, std::string> table);
    // The meaning map, as a last resort for words nothing else recognized.
    // Not owned; may be null. threshold: how close counts as meaning it.
    void set_vectors(const Vectors* v, double threshold = 0.6);

    // prev: the previous reading, so "and the day before?" can be answered.
    Reading read(const std::string& question, int64_t now,
                 const Reading* prev = nullptr) const;

private:
    std::vector<std::string> programs_;
    std::map<std::string, std::string> synonyms_;
    const Vectors* vectors_ = nullptr;
    double vector_threshold_ = 0.6;
};

// Edit distance for typo-tolerant matching ("blendr" -> "blender").
int edit_distance(const std::string& a, const std::string& b, int limit);

// ---- the realizer: grammatical English from facts, by rule ----

struct NP {                       // a noun phrase: "blender", "three programs"
    std::string noun;             // singular base form, or a proper name
    bool proper = false;          // proper names skip articles and casing fixes
    long count = -1;              // -1: no number; 1: singular; n: "three programs"
    std::string detail;           // trailing detail: "programs *you started*"
};

enum class Tense { Past, Present };

struct Clause {                   // subject + verb + the rest
    NP subject;
    std::string verb;             // base form: "grow", "be", "use"
    std::string rest;             // "by 1.4 GB", "74% of the CPU"
    Tense tense = Tense::Past;
};

// Sentence assembly, each piece on its own:
std::string join_list(const std::vector<std::string>& items);      // "a, b and c"
std::string plural_noun(const std::string& noun, long n);          // process -> processes
std::string number_word(long n);                                   // 3 -> "three"
std::string past_tense(const std::string& verb, bool plural_subject);
std::string present_tense(const std::string& verb, bool plural_subject);
std::string indefinite(const std::string& noun);                   // "an event", "a program"

// Clauses to prose: merges clauses that share a subject ("blender grew by
// 1.4 GB and held 852 MB"), and uses a pronoun when a subject returns
// ("It also ..."). Deterministic; same facts, same sentence, every time.
std::string realize(const std::vector<Clause>& clauses);

// ---- retrieval: BM25 relevance over the caller's records ----

struct Hit {
    int64_t id = 0;
    int64_t t = 0;
    std::string kind, text;
    double score = 0;  // higher is better
};

// A search index over short records (event summaries, log lines). Backed by
// SQLite FTS5 with the porter stemmer, so "crashed" finds "crashing". The
// understanding half narrows the when and the what; this ranks the records.
class Index {
public:
    Index();                        // in memory
    explicit Index(const std::string& path);
    ~Index();
    bool ok() const { return db_ != nullptr; }
    void add(int64_t id, int64_t t, const std::string& kind, const std::string& text);
    // terms are OR-ed; hits outside [from, to) are excluded; best first.
    std::vector<Hit> search(const std::vector<std::string>& terms,
                            int64_t from, int64_t to, int limit = 12) const;

private:
    struct sqlite3* db_ = nullptr;
};

// The guaranteed reply for a question the library did not understand well
// enough to answer. Never empty: it always says, one way or another,
// "I can't answer that", and names what little it did recognize.
std::string refuse(const Reading& r, const std::string& domain_name);

}  // namespace unllm
