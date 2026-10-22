// The extractor exam: answer types, spans, number words and date keys, one
// check per row of tests/extract_cases.tsv, then the reformulation matcher
// on sentences shaped like the world exam's fixtures.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "unllm/extract.h"

using namespace unllm;

static int checks = 0, failures = 0;

static void check(bool ok, const std::string& id, const std::string& got, const std::string& want) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("  FAIL %s\n        got  [%s]\n        want [%s]\n", id.c_str(), got.c_str(), want.c_str());
}

static std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

static std::string shape_name(const AnswerShape& s) {
    std::string n = answer_type_name(s.type);
    if (s.also != AnswerType::None) n += std::string("/") + answer_type_name(s.also);
    if (s.type == AnswerType::List) n += std::string(":") + answer_type_name(s.item);
    return n;
}

// "text => key | text" as the TSV writes spans; keys only where the row gives them.
static std::string spans_text(const std::vector<Span>& spans, const std::string& want) {
    if (spans.empty()) return "(none)";
    std::string out;
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (true) {
        std::size_t k = want.find(" | ", at);
        parts.push_back(want.substr(at, k == std::string::npos ? std::string::npos : k - at));
        if (k == std::string::npos) break;
        at = k + 3;
    }
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (i) out += " | ";
        out += spans[i].text;
        if (i < parts.size() && parts[i].find(" => ") != std::string::npos) out += " => " + spans[i].key;
    }
    return out;
}

static Span span_in(const std::string& sentence, AnswerType type, const std::string& text) {
    for (const auto& s : spans_of(sentence, type))
        if (s.text == text) return s;
    return Span{};
}

int main() {
    AnswerTypes types;
    Reformulations refs;
    ++checks;
    if (!types.load("data/answertypes.txt") || !refs.load("data/reformulations.txt") ||
        !load_span_units("data/units.txt")) {
        ++failures;
        std::printf("  FAIL: tables did not load\n");
    }

    std::ifstream f("tests/extract_cases.tsv");
    ++checks;
    if (!f) {
        ++failures;
        std::printf("  FAIL: tests/extract_cases.tsv is missing\n");
    }
    std::string line;
    int rows = 0;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto c = split_tabs(line);
        if (c.size() < 5) continue;
        ++rows;
        const std::string &id = c[0], &fn = c[1], &input = c[2], &arg = c[3], &want = c[4];
        if (fn == "answer_type") {
            std::string got = shape_name(answer_type(types, input));
            check(got == want, id, got, want);
        } else if (fn == "spans_of") {
            std::string got = spans_text(spans_of(input, answer_type_from_name(arg)), want);
            check(got == want, id, got, want);
        } else if (fn == "parse_number_words") {
            auto v = parse_number_words(input);
            std::string got = v ? std::to_string(*v) : "(none)";
            bool ok = want == "(none)" ? !v : (v && std::fabs(*v - std::stod(want)) < 1e-9);
            check(ok, id, got, want);
        } else if (fn == "date_key") {
            std::string got = date_key(input);
            if (got.empty()) got = "(none)";
            check(got == want, id, got, want);
        } else {
            check(false, id, "unknown function " + fn, "");
        }
    }
    check(rows == 71, "row count", std::to_string(rows), "71");

    // ---- more answer types ----
    check(shape_name(answer_type(types, "who is singing tonight")) == "person", "X01 ing word is not a bare name",
          shape_name(answer_type(types, "who is singing tonight")), "person");
    check(shape_name(answer_type(types, "")) == "description", "X02 empty", shape_name(answer_type(types, "")),
          "description");
    check(shape_name(answer_type(types, "is it")) == "description", "X03 only stop words",
          shape_name(answer_type(types, "is it")), "description");
    check(answer_type(types, "what is the population of port wren").head == "population", "X04 head kept",
          answer_type(types, "what is the population of port wren").head, "population");
    check(shape_name(answer_type(types, "which studio recorded paper lanterns")) == "organization",
          "X05 studio is an organization", shape_name(answer_type(types, "which studio recorded paper lanterns")),
          "organization");

    check(shape_name(answer_type(types, "what did ada example invent")) == "title", "X06 what did X invent",
          shape_name(answer_type(types, "what did ada example invent")), "title");

    // ---- more numbers and dates ----
    struct NumRow { const char* in; double want; bool ok; };
    const NumRow nums[] = {{"thousand million", 0, false}, {"two big million", 0, false}, {"21st", 0, false},
                           {"12,3456", 0, false},         {"1.2.3", 0, false},           {"two dozen", 24, true},
                           {"one thousand and five", 1005, true}, {"ninety-nine", 99, true}};
    for (const auto& r : nums) {
        auto v = parse_number_words(r.in);
        bool ok = r.ok ? (v && std::fabs(*v - r.want) < 1e-9) : !v;
        check(ok, std::string("X10 number ") + r.in, v ? std::to_string(*v) : "(none)",
              r.ok ? std::to_string(r.want) : "(none)");
    }
    struct DateRow { const char* in; const char* want; };
    const DateRow dates[] = {{"29 February 1801", ""}, {"29 February 1804", "1804-02-29"}, {"May", ""},
                             {"12/07/1776", ""},       {"76", ""},                       {"c. 1776", "1776"},
                             {"the 4th of July", "--07-04"}, {"Jan 1990", "1990-01"},     {"4 July 1776 then", ""}};
    for (const auto& r : dates) {
        std::string got = date_key(r.in);
        check(got == r.want, std::string("X11 date ") + r.in, got, r.want);
    }
    check(keys_agree("1776", "1776-07-04") && keys_agree("--07-04", "1776-07-04") && keys_agree("1776-07", "1776") &&
              !keys_agree("1776", "17760") && !keys_agree("1776-07-04", "1776-07-05") && !keys_agree("14", "14 km"),
          "X12 keys_agree", "", "");

    // ---- more spans ----
    check(spans_text(spans_of("He scored in the A320 and COVID-19 years.", AnswerType::Number), "") == "(none)",
          "X20 digits glued to letters", spans_text(spans_of("He scored in the A320 and COVID-19 years.", AnswerType::Number), ""),
          "(none)");
    {
        std::string s = "Nineteen eighty-four was written in 1948.";
        std::string got = spans_text(spans_of(s, AnswerType::Number), "");
        check(got == "(none)", "X22 a year read aloud is not two numbers", got, "(none)");
    }
    {
        std::string s = "The Quiet Harbours are a folk rock band from Port Wren.";
        auto sp = spans_of(s, AnswerType::Organization);
        bool ok = !sp.empty() && sp[0].text == "The Quiet Harbours" && sp[0].key == "quiet harbours" &&
                  s.substr(sp[0].start, sp[0].end - sp[0].start) == sp[0].text;
        check(ok, "X21 byte offsets", sp.empty() ? "" : sp[0].text, "The Quiet Harbours");
    }

    // ---- reformulations ----
    auto weight = [&](const std::string& q, const std::string& sentence, const std::string& text,
                      const MatchContext* ctx = nullptr) {
        AnswerShape shape = answer_type(types, q);
        auto ps = refs.bind(q, shape);
        Span a = span_in(sentence, shape.type, text);
        if (a.text.empty() && shape.also != AnswerType::None) a = span_in(sentence, shape.also, text);
        if (a.text.empty()) return -1.0;
        return Reformulations::match(ps, sentence, a, ctx);
    };
    auto expect = [&](const char* id, double got, double want) {
        check(got == want, id, std::to_string(got), std::to_string(want));
    };
    const std::string written =
        "Paper Lanterns was written by Mara Quill and recorded by The Quiet Harbours at Saltmarsh Studio in the "
        "spring of 1993.";
    expect("R01 recorded by", weight("who sang paper lanterns", written, "The Quiet Harbours"), 3);
    expect("R02 not the writer", weight("who sang paper lanterns", written, "Mara Quill"), 0);
    expect("R03 written by", weight("who wrote paper lanterns", written, "Mara Quill"), 5);
    expect("R04 recorded by needs the song",
           weight("who sang paper lanterns", "It was recorded by The Quiet Harbours.", "The Quiet Harbours"), 0);
    const std::string cast =
        "The series stars Jane Roe as Saul, Tom Bell as Iris Marsh, and Lee Park as the narrator.";
    expect("R05 name before as", weight("who plays saul in harbour lights", cast, "Jane Roe"), 5);
    expect("R06 as binds the nearest", weight("who played iris marsh in harbour lights", cast, "Tom Bell"), 5);
    expect("R07 not the first in the list", weight("who played iris marsh in harbour lights", cast, "Jane Roe"), 0);
    MatchContext rusty{"Rusty (character)", "Rusty is a talking fox in the animated film The Clockwork Fox."};
    const std::string voiced = "He was voiced by Max Voice, who also sang the closing song.";
    expect("R08 pronoun from the title", weight("who voiced rusty in the clockwork fox", voiced, "Max Voice", &rusty),
           5);
    MatchContext other{"The Clockwork Fox", ""};
    expect("R09 pronoun needs the subject",
           weight("who voiced rusty in the clockwork fox", voiced, "Max Voice", &other), 3);
    MatchContext harbour{"Harbour Lights (TV series)", ""};
    expect("R10 pronoun and a date",
           weight("when did harbour lights first air", "It first aired on 3 March 2015.", "3 March 2015", &harbour), 5);
    expect("R11 a star in the middle",
           weight("who directed the clockwork fox",
                  "The Clockwork Fox is a 2003 animated film directed by Lena Brook.", "Lena Brook"),
           5);
    expect("R12 number words with the noun inside the span",
           weight("how many people visit glass lake each year",
                  "About two million people visit Glass Lake each year.", "two million people"),
           5);
    expect("R13 digits with the noun inside the span",
           weight("how many people visit glass lake each year",
                  "Glass Lake draws 2,000,000 visitors a year, more than any other site in Testland.",
                  "2,000,000 visitors"),
           5);
    MatchContext lake{"Glass Lake", ""};
    expect("R14 length",
           weight("how long is glass lake", "It is 14 kilometres long and very cold all year.", "14 kilometres", &lake),
           5);
    expect("R15 date before the verb",
           weight("when was the testland charter signed",
                  "On July 4, 1776, delegates signed the Testland Charter in its main room.", "July 4, 1776"),
           5);
    expect("R16 located in",
           weight("where is saltmarsh studio",
                  "Saltmarsh Studio is a recording studio located in Port Wren, a fishing town on the east coast of "
                  "Testland.",
                  "Port Wren"),
           5);
    expect("R17 not the far place",
           weight("where is saltmarsh studio",
                  "Saltmarsh Studio is a recording studio located in Port Wren, a fishing town on the east coast of "
                  "Testland.",
                  "Testland"),
           0);
    expect("R18 never says population",
           weight("what is the population of port wren", "The old fort at Port Wren draws 450,000.", "450,000"), 0);
    check(refs.bind("why is the sky blue", answer_type(types, "why is the sky blue")).empty(), "R19 no line applies",
          "", "");

    // ---- cost: a few microseconds per sentence ----
    {
        auto t0 = std::chrono::steady_clock::now();
        std::size_t found = 0;
        const int reps = 2000;
        for (int i = 0; i < reps; ++i)
            for (AnswerType t : {AnswerType::Person, AnswerType::Organization, AnswerType::Number, AnswerType::Date})
                found += spans_of(written, t).size();
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() /
                    (reps * 4.0);
        std::printf("extract: %.2f us per sentence and type (%zu spans)\n", us, found);
        check(us < 50, "X30 spans_of is fast", std::to_string(us), "< 50 us");
    }

    std::printf("extract: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
