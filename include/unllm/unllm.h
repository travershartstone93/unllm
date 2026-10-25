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
// Whether the question is about something that happened ("was", "did",
// "crashed") rather than something happening now ("is", "crashing").
bool past_tense(const std::vector<Token>& tokens);
std::string normalize_word(const std::string& lower);  // lemma table + light stemmer

// ---- what a question is asking ----

enum class Intent {
    Heat, Slow, Memory, Disk, Network, Sharing, Update, Config, Crash,
    Trust, Battery, Startup,
    Devices,   // hardware coming and going: usb, webcams, dongles
    Flash,     // writing firmware to boards: esp32, arduino
    Gpu,       // the graphics card: load, video memory, temperature
    Diagnose,  // "what is wrong with this machine": run the rule book
    Identity,  // the machine itself: what it is, its parts, its uptime
    Help,      // what can be asked
    Story,     // no specific topic, tell everything
};
const char* intent_name(Intent i);

// The shape of the question: an open question wants facts, a polar one wants
// yes or no, a counting one wants a number.
enum class Form { Open, Polar, Count };

// The shape of the ANSWER the question expects: "when" wants a moment, not a
// story; "why" wants the cause first; "how long" wants a duration.
enum class Wants { Fact, Time, Duration, Count, Quantity, Name, Cause, YesNo };
const char* wants_name(Wants w);

// Judgment questions: "is that normal", "hotter than yesterday", "getting
// slower", "did it change after the update". The caller answers them with
// statistics over its own records.
enum class Judge { None, Normal, Compare, Trend, Changed };

// The kind of request beyond its topic: a summary, a worry, a request for
// advice, a choice between two things, a condition ("while X is running"),
// and the world-knowledge kinds the caller may answer from reference packs.
enum class Frame {
    None, Define, Explain, HowTo, Choice, Condition, Worry, Advice,
    Summary, Calc, Poem, Opinion, Creative, NeedsNetwork, Personal,
};
const char* frame_name(Frame f);

// Superlatives: "the hottest", "the most memory". Detected before the
// stemmer folds "hottest" down to "hot".
enum class Degree { None, Most, Least };

struct Entity {
    std::string type;   // "program", "quantity", "pid"
    std::string value;  // canonical form: "blender", "90 C", "2671"
};

struct TimeWindow {
    int64_t from = 0, to = 0;  // unix seconds, half-open [from, to)
    bool stated = false;       // false: the question named no time, default applied
    bool live = false;         // "right now": the answer should be measured, not recalled
    bool present = false;      // no time named and the verb in the present tense ("is the disk busy"):
                               // about now for a question already known to be about this machine
    bool shifted = false;      // "this morning" asked before it began: moved back a day
    bool all_history = false;  // no time named, and a past-tense "when" question ("when did i set up the
                               // power cap"): the moment may lie anywhere in the records. from/to keep
                               // the last-day default; the caller decides how far back to look.
    bool sleep = false;        // "while i was asleep": the night just past, for the caller to narrow to the stretch its
                               // records show the person doing nothing
    int64_t at = 0;            // a named moment ("at 3am", "around 9:20", "2 hours ago"): the moment itself, the
                               // window centred on it; 0 for a span
};

struct Reading {
    TimeWindow window;
    Form form = Form::Open;
    Wants wants = Wants::Fact;
    Judge judge = Judge::None;
    bool excess = false;          // "a lot", "too much": a Normal judge asking whether it is above the usual
    std::optional<TimeWindow> compare_window;  // "than yesterday": the referent
    // A second time named and set against the first: "hotter yesterday morning or last night" (window:
    // yesterday morning; this: last night), "hotter today than yesterday". judge is Compare. With
    // "than", compare_window holds the same window as before.
    std::optional<TimeWindow> second_window;
    Degree degree = Degree::None;
    bool negated = false;     // "never", "didn't": the question denies something
    bool self_named = false;  // the question names the machine itself
    std::vector<Intent> intents;    // strongest first; empty means Story
    std::vector<Entity> entities;
    std::vector<std::string> terms;  // normalized content words, for retrieval
    std::vector<double> weights;     // attention weight per entry of terms (long questions)
    Frame frame = Frame::None;
    bool self_deictic = false;       // "this/my/the machine": the machine as the subject
    bool anchored = false;           // tied to this machine: a time, a program, the machine, now
    std::vector<std::string> choices;    // "firefox or chromium": the two candidates
    // "was it the ram or the swap": two things that are not programs (nor times) set side by side by "or",
    // as typed ("ram", "swap"), for the caller to tell apart by meaning. Empty when choices holds them.
    std::vector<std::string> alternatives;
    std::string condition;               // "while blender is running": the program
    // "while i watched the movie": a condition no program name settles. frame is Condition; condition
    // stays empty unless the clause names a known program; the caller names the program from these.
    std::string condition_clause;        // the clause as typed: "i watched the movie"
    std::string condition_thing;         // what it is about: "movie" ("i was gaming": "gaming")
    // "in the half hour before the terminal got killed", "since it got killed": a span tied to an event
    // only the caller's records can place. window still holds the day words ("yesterday morning").
    // A program named in the clause names the event, not the subject: it is not in entities.
    std::string event_clause;            // the event as typed: "the terminal got killed"
    int64_t event_span = 0;              // the span's length in seconds: 1800. 0: no length named, so
                                         // from window.from up to the event (before), or from the event
                                         // to window.to (after, since)
    bool event_after = false;            // the span follows the event ("after", "since"), not before it
    bool event_at = false;               // the question is about the event's moment ("what was running when the
                                         // machine shut off"), not a span before or after it
    bool event_during = false;           // "disk writes during the update": the span is the event's own time
    std::string predicate;               // a yes/no question's head word: "healthy", "crash"
    std::string subject;                 // a general subject: "computer memory" (world questions)
    std::vector<std::string> ambiguous;  // "it" with two candidates: both, never guessed
    bool asks_doer = false;              // "what's using my bandwidth": the question asks what is doing it
    // How much of the question the library actually understood: matched
    // meaningful words over all meaningful words. Near 1: on home ground.
    // Near 0: this question is not about the domain; say so, do not guess.
    double domain = 0;
};

// ---- time phrases ----

// Finds a time expression in the tokens and resolves it against `now`
// (injected, so tests are stable). Consumed token indices are reported so
// they do not pollute the search terms. A bare weekday is the most recent
// one; "this morning" asked before the morning began moves back a day and
// is marked shifted.
std::optional<TimeWindow> find_time(const std::vector<Token>& tokens, int64_t now,
                                    std::set<std::size_t>* consumed = nullptr);

// The length in seconds of a span named just before tokens[end] ("the half
// hour" in "the half hour before the terminal got killed", "10 minutes",
// "an hour"); 0 when there is none. first receives its first word's index.
int64_t span_before(const std::vector<Token>& tokens, std::size_t end, std::size_t* first = nullptr);

// ---- the analyzer ----

// ---- the meaning map: static word vectors, lookups and arithmetic ----
// A table distilled once from a language model; using it involves no model:
// a sentence's meaning is the mean of its word rows, similarity is a dot
// product. File format: "UNLV", u32 count, u32 dim, then per word a u8
// length, the word's bytes, and dim float32 coordinates.

// A second, larger format ("UNL2") holds hundreds of thousands of words as
// 8-bit rows with a per-row scale, read straight from disk (memory-mapped):
// u32 count, u32 dim, u32 offsets[count+1] into a sorted string blob,
// f32 scales[count], i8 rows[count*dim], then the blob. Rows are unit length
// before quantizing, so a dot product times both scales is the cosine.
class Vectors {
public:
    Vectors() = default;
    ~Vectors();
    Vectors(const Vectors&) = delete;
    Vectors& operator=(const Vectors&) = delete;
    bool load(const std::string& path);
    bool ok() const { return dim_ > 0; }
    std::size_t size() const { return map_ ? count_ : words_.size(); }
    bool contains(const std::string& w) const;
    // Cosine similarity in [-1, 1]; 0 when either word is unknown.
    double similarity(const std::string& a, const std::string& b) const;

private:
    const float* row(const std::string& w) const;
    long find(const std::string& w) const;  // UNL2 row index, or -1
    std::map<std::string, std::size_t> words_;
    std::vector<float> data_;
    std::vector<float> norms_;
    std::size_t dim_ = 0;
    // UNL2, memory-mapped
    const char* map_ = nullptr;
    std::size_t map_size_ = 0, count_ = 0;
    const uint32_t* offsets_ = nullptr;
    const float* scales_ = nullptr;
    const int8_t* rows_ = nullptr;
    const char* blob_ = nullptr;
};

// Attention over the meaning map: one weight per token, how much that word
// carries in this question. Stop words weigh nothing, known filler almost
// nothing; a content word weighs more the closer it sits to the topic words
// and to the rest of the question. idf, when given, scales by rarity.
std::vector<double> attend(const std::vector<Token>& tokens, const Vectors* v,
                           double (*idf)(const std::string&) = nullptr);

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
    // salience: who "it" may mean, most recent first. Group 0 is usually
    // what the previous ANSWER named, group 1 the previous question's
    // programs. "It" binds only to a lone name in the first non-empty group;
    // two names there leave Reading::ambiguous filled instead of guessing.
    Reading read(const std::string& question, int64_t now, const Reading* prev,
                 const std::vector<std::vector<std::string>>* salience) const;

    // Every word the analyzer recognizes on its own: topic, question,
    // time, negation, degree and generic words, synonyms, and (optionally)
    // programs. The speller corrects toward these and the coverage report
    // ranks the rest.
    std::vector<std::string> known_words(bool include_programs = true) const;

private:
    std::vector<std::string> programs_;
    std::map<std::string, std::string> synonyms_;
    const Vectors* vectors_ = nullptr;
    double vector_threshold_ = 0.6;
};

// Where a question should be answered from:
//   Domain    the caller's own records (the machine), always first when anchored
//   World     reference knowledge: definitions, explanations, how-to, poems
//   Category  a fixed honest reply: needs the internet, asks an opinion,
//             asks for original writing
// (Calculation is recognized by the Calc parser itself, before routing.)
enum class Route { Domain, World, Category };
Route route(const Reading& r);
const char* route_name(Route r);

// The analyzer's stop words ("the", "what", "did"...), for callers that pick
// a question's content words themselves.
bool stop_word(const std::string& lower);

// Where a second question starts in one line ("what's the battery health and how fast is it draining":
// at "how"), as a byte offset into it; npos when the line asks one question.
std::size_t second_question(const std::string& question);

// Edit distance for typo-tolerant matching ("blendr" -> "blender").
int edit_distance(const std::string& a, const std::string& b, int limit);

// ---- the realizer: grammatical English from facts, by rule ----

struct NP {                       // a noun phrase: "blender", "three programs"
    std::string noun;             // singular base form, or a proper name
    bool proper = false;          // proper names skip articles and casing fixes
    long count = -1;              // -1: no number; 1: singular; n: "three programs"
    std::string detail;           // trailing detail: "programs *you started*"
    bool plural = false;          // "the disks", "firefox and chromium"
};

enum class Tense { Past, Present, PresentPerfect };

struct Clause {                   // subject + verb + the rest
    NP subject;
    std::string verb;             // base form: "grow", "be", "use"
    std::string rest;             // "by 1.4 GB", "74% of the CPU"
    Tense tense = Tense::Past;
    bool negated = false;         // "did not crash", "is not on battery"
    bool contrast = false;        // joined with ", but ..." to what came before
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

// A yes/no answer: "Yes, " or "No, " followed by the evidence sentence.
// When the evidence already opens with a "No ..." count, it stands alone.
std::string realize_polar(bool yes, const std::vector<Clause>& clauses);

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
// "I can't answer that", and names what little it did recognize. It never
// denies the domain to a question that named the domain itself.
std::string refuse(const Reading& r, const std::string& domain_name);

// ---- banter: fixed, palatable replies to the classics ----
// "what is the meaning of life" deserves better than a cold refusal. The
// mechanism lives here; the wording is data (data/banter.txt): lines of
// "trigger ; trigger :: reply". A trigger starting with '~' matches as a
// phrase inside the question; all others must be the whole question. Both
// sides are compared after normalization, so word endings do not matter.
class Banter {
public:
    bool load(const std::string& path);
    // The fixed reply, or "" when the question is not one of the classics.
    std::string match(const std::vector<Token>& tokens) const;
    void add(const std::string& line);  // one "trigger ; trigger :: reply" line
    std::size_t size() const { return exact_.size() + within_.size(); }

private:
    std::map<std::string, std::string> exact_;                       // whole question
    std::vector<std::pair<std::string, std::string>> within_;        // phrase inside
};

// ---- categories: honest fixed replies for what no record can answer ----
// Live news, opinions, new creative writing. data/categories.txt holds one
// [section] per category (route frame names: network, opinion, creative),
// each with banter-format lines and a "* :: reply" default.
class Categories {
public:
    bool load(const std::string& path);
    // The reply for a question in that category; "" when the file has none.
    std::string reply(const std::string& category, const std::vector<Token>& tokens) const;

private:
    std::map<std::string, Banter> tables_;
    std::map<std::string, std::string> defaults_;
};

// ---- rewrites: thousands of phrasings fold onto a few ----
// "could you tell me what crashed" is "what crashed" wearing a coat. The
// mechanism lives here; the reductions are data (data/rewrites.txt): lines of
// "pattern -> replacement" over normalized words, one * wildcard allowed on
// each side. The caller applies this to the question string BEFORE analysis.
class Rewrites {
public:
    bool load(const std::string& path);
    std::string apply(const std::string& question) const;  // at most 3 passes
    std::size_t size() const { return rules_.size(); }

private:
    struct Rule {
        std::vector<std::string> pat;  // normalized tokens; "*" binds 1+ tokens
        std::vector<std::string> rep;  // literal words; "*" re-emits the bound tokens
    };
    std::vector<Rule> rules_;
};

// ---- spelling: chat-speak and typos, before anything else reads ----
// "wat crashd 2day" is "what crash today". Chat-speak is a table
// (data/chatspeak.txt, "word -> replacement words"). A typo is corrected
// only when the word is unknown everywhere, five letters or longer, and
// exactly one known word family lies one keystroke away with the same first
// letter. Real words of the language (the protected set, e.g. a dictionary
// lemma list) are never "corrected" into machine words.
class Speller {
public:
    bool load_chatspeak(const std::string& path);
    void set_known(const std::vector<std::string>& words);
    void set_protected(const std::set<std::string>* words) { protected_ = words; }
    // The question with chat-speak expanded and typos fixed, lowercased.
    // changes, when given, receives each (typed, corrected) pair.
    std::string fix(const std::string& question,
                    std::vector<std::pair<std::string, std::string>>* changes = nullptr) const;

private:
    std::map<std::string, std::string> chatspeak_;
    std::set<std::string> known_;
    std::set<std::string> known_norm_;
    const std::set<std::string>* protected_ = nullptr;
};

}  // namespace unllm
