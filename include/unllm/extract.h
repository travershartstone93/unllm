#pragma once
// The answer extractor's building blocks: what kind of short answer a
// question wants (a person, a date, a number...), the pieces of a sentence
// that could be that answer, and the sentences a passage would use to state
// it. Everything is table lookups and string scans: no vectors, no
// randomness, the same input always gives the same output.

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace unllm {

// ---- what kind of answer a question wants ----

enum class AnswerType {
    None, Person, Place, Date, Year, Number, Duration, Organization,
    Title, List, Description,
};
const char* answer_type_name(AnswerType t);  // "person", "list", ...
// The reverse: "person" -> Person. None for an unknown name.
AnswerType answer_type_from_name(const std::string& name);

struct AnswerShape {
    AnswerType type = AnswerType::Description;
    AnswerType also = AnswerType::None;  // the second type of person/organization
    AnswerType item = AnswerType::None;  // for List: each item's type
    std::string head;                    // the head noun that decided it, "" if a pattern did
};

// data/answertypes.txt: [patterns], [heads] and [events].
class AnswerTypes {
public:
    bool load(const std::string& path);
    // One line of the file, as it would appear under [section] (no brackets).
    void add(const std::string& line, const std::string& section);
    AnswerShape classify(const std::string& question) const;
    // The type a head noun names ("river" -> Place), None when unknown.
    AnswerType head_type(const std::string& noun) const;

private:
    struct Rule {
        std::vector<std::vector<std::string>> alternatives;  // pattern words, "*" and "{N}"
        enum Direction { Type, Head, Measure, List } direction = Type;
        AnswerType type = AnswerType::Description;
        AnswerType also = AnswerType::None;
    };
    std::vector<Rule> rules_;
    std::map<std::string, AnswerType> heads_;
    std::set<std::string> events_;
};

AnswerShape answer_type(const AnswerTypes& table, const std::string& question);

// ---- the pieces of a sentence that could answer ----

struct Span {
    std::string text;  // as written in the sentence
    std::string key;   // canonical form, for voting
    AnswerType type = AnswerType::None;
    std::size_t start = 0;  // byte offsets [start, end) in the sentence
    std::size_t end = 0;
};

// Every candidate of that type in the sentence, in sentence order, each key
// once (at its first place). List, Description and None give nothing.
std::vector<Span> spans_of(const std::string& sentence, AnswerType type);

// The units numbers may carry (data/units.txt). spans_of loads
// "data/units.txt" from the working directory on first use when this was
// never called; call it at startup, before any spans_of, to use another file.
bool load_span_units(const std::string& path);

// "two million" -> 2000000, "one hundred and five" -> 105, "2,000,000",
// "2.5 million". nullopt unless the whole phrase is one number.
std::optional<double> parse_number_words(const std::string& phrase);

// "4 July 1776" -> "1776-07-04", "July 1776" -> "1776-07", "4 July" ->
// "--07-04", "the 1990s" -> "1990s", "500 BC" -> "-500"; "" when the whole
// text is not one date.
std::string date_key(const std::string& text);

// Two date keys agree when equal or when one is the start of the other at a
// field boundary ("1776" and "1776-07-04"; "--07-04" and "1776-07-04").
// Other keys agree only when equal.
bool keys_agree(const std::string& a, const std::string& b);

// ---- the sentences a passage would use to state the answer ----

// One statement of data/reformulations.txt with the question's words bound
// into it: "{X} was recorded by {A}" for "who sang paper lanterns" holds
// the words "paper lanterns" where {X} was.
struct Pattern {
    struct Item {
        enum Kind { Word, Slot, Answer, Star } kind = Word;
        std::string word;                // Word: one lowercase word or punctuation mark
        char slot = 0;                   // Slot: 'X', 'Y', 'Z' or 'R'
        std::vector<std::string> words;  // Slot: the question's words bound to it
    };
    std::vector<Item> items;
    int weight = 0;                         // 1 to 5
    AnswerType type = AnswerType::None;     // what {A} must be
    AnswerType also = AnswerType::None;     // or this (person/organization)
    std::vector<std::string> need;          // words the sentence must hold ({X} when the statement lacks it)
    std::string statement;                  // as written in the table, for traces
};

// Where a sentence sits, for reading a leading pronoun as the subject:
// "He was voiced by Max Voice" in the article "Rusty (character)".
struct MatchContext {
    std::string title;     // the passage's document title
    std::string previous;  // the sentence before this one in the same passage, "" for the first
};

// A sentence cut into words once, so several candidates can be tried
// against it without cutting it again.
struct PreparedSentence {
    struct Word {
        std::string lower;
        std::size_t start = 0, end = 0;
    };
    std::vector<Word> words;
    std::set<std::string> vocabulary;  // every lowercase word, for the {X} check
    bool pronoun_first = false;        // starts with he, she, it or they
    std::string title_key;             // the context's title, brackets removed, title_key()
    std::vector<std::string> previous; // the previous sentence's first words, lowercase
};
PreparedSentence prepare_sentence(const std::string& sentence, const MatchContext* context = nullptr);

class Reformulations {
public:
    bool load(const std::string& path);
    void add(const std::string& line);
    std::size_t size() const { return lines_.size(); }
    // The statements of every line whose question side matches, with the
    // question's words bound in. Empty when no line matches. shape fills in
    // the type of lines marked "*".
    std::vector<Pattern> bind(const std::string& question, const AnswerShape& shape) const;
    // The highest weight of a pattern that matches the sentence with {A}
    // standing exactly where the candidate stands (and of a type the pattern
    // takes); 0 when none does.
    static double match(const std::vector<Pattern>& patterns, const PreparedSentence& sentence,
                        const Span& candidate);
    static double match(const std::vector<Pattern>& patterns, const std::string& sentence, const Span& candidate,
                        const MatchContext* context = nullptr);

private:
    struct Line {
        std::vector<std::vector<Pattern::Item>> questions;
        std::vector<std::pair<std::vector<Pattern::Item>, std::string>> statements;
        std::vector<int> weights;
        bool keep_type = false;  // "*": the question's own types
        AnswerType type = AnswerType::None, also = AnswerType::None;
    };
    std::vector<Line> lines_;
};

}  // namespace unllm
