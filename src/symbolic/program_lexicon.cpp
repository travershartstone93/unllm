#include "gui_json.h"
#include "unllm/symbolic/grounding.h"
#include <algorithm>
#include <set>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/utf8.h>
namespace unllm::native {
namespace {
struct Token {
  std::string word;
  int start, end, byte, end_byte;
};
bool alnum(UChar32 cp) {
  auto kind = u_charType(cp);
  return (kind >= U_UPPERCASE_LETTER && kind <= U_OTHER_LETTER) ||
         (kind >= U_DECIMAL_DIGIT_NUMBER && kind <= U_OTHER_NUMBER);
}
std::vector<Token> tokenize(const std::string &s) {
  std::vector<Token> out;
  int32_t length = s.size(), i = 0;
  int index = 0;
  while (i < length) {
    int start = i, offset = index++;
    UChar32 cp;
    U8_NEXT(s.data(), i, length, cp);
    if (cp < 0)
      throw Unsupported("Invalid UTF-8 in programming request");
    if (u_isUWhiteSpace(cp) || (cp >= 28 && cp <= 31))
      continue;
    if (alnum(cp) || cp == '_')
      while (i < length) {
        auto end = i;
        U8_NEXT(s.data(), end, length, cp);
        if (!alnum(cp) && cp != '_' && cp != '\'')
          break;
        i = end;
        ++index;
      }
    auto token = icu::UnicodeString::fromUTF8(s.substr(start, i - start));
    token.foldCase();
    std::string word;
    token.toUTF8String(word);
    out.push_back({word, offset, index, start, i});
  }
  return out;
}
} // namespace
bool creation_request(const std::string &text) {
  auto tokens = tokenize(text);
  static const std::set<std::string> verbs{"write", "create", "generate",
                                           "build", "make"};
  return !tokens.empty() && verbs.contains(tokens.front().word);
}
ProgramText normalize_program(const std::string &source, const Json &lexicon) {
  using namespace gui;
  auto tokens = tokenize(source);
  struct Edit {
    Token last;
    std::string surface, concept_id, kind;
  };
  std::map<size_t, Edit> edits;
  for (auto e : lexicon["aliases"].items()) {
    auto pattern = tokenize(e["term"].text());
    if (pattern.empty())
      continue;
    for (size_t i = 0; i + pattern.size() <= tokens.size(); ++i)
      if (std::equal(
              pattern.begin(), pattern.end(), tokens.begin() + i,
              [](const Token &a, const Token &b) { return a.word == b.word; }))
        edits[i] = {tokens[i + pattern.size() - 1], e["surface"].text(),
                    e["concept"].text(), "noun"};
  }
  for (size_t i = 0; i < tokens.size(); ++i) {
    auto word = tokens[i].word;
    if (word != "button" && word != "buttons" && word != "control" &&
        word != "controls")
      continue;
    for (size_t j = i; j > 0;) {
      auto &t = tokens[--j];
      if (t.word == "," || t.word == "and" || t.word == "or")
        continue;
      if (!lexicon["action_labels"].contains(t.word))
        break;
      edits[j] = {t, "operation", lexicon["action_labels"][t.word].text(),
                  "action"};
    }
  }
  ProgramText out;
  int byte = 0, delta = 0;
  for (auto &[i, e] : edits) {
    auto &t = tokens[i];
    if (t.byte < byte)
      continue;
    out.text += source.substr(byte, t.byte - byte) + e.surface;
    auto size = icu::UnicodeString::fromUTF8(e.surface).countChar32();
    auto start = t.start + delta;
    out.spans.push(
        obj({{"start", num(start)},
             {"end", num(start + size)},
             {"source_span", arr({num(t.start), num(e.last.end)})},
             {"source", str(source.substr(t.byte, e.last.end_byte - t.byte))},
             {"concept", str(e.concept_id)},
             {"kind", str(e.kind)}}));
    delta += size - (e.last.end - t.start);
    byte = e.last.end_byte;
  }
  out.text += source.substr(byte);
  return out;
}
void annotate_program(Json semantic, const Json &spans,
                      const std::string &source) {
  auto input = icu::UnicodeString::fromUTF8(source);
  auto edits = spans.items();
  auto original = [&](int offset, bool right) {
    int delta = 0;
    for (const auto &edit : edits) {
      int begin = edit["start"].integer(), end = edit["end"].integer();
      auto source = edit["source_span"];
      if (offset <= begin)
        break;
      if (offset < end)
        return int(source.at(right ? 1 : 0).integer());
      delta = source.at(1).integer() - end;
    }
    return offset + delta;
  };
  for (auto ep : semantic.items()) {
    auto span = ep["span"];
    if (span.size() != 2 || span.at(0).integer() < 0)
      continue;
    int begin = original(span.at(0).integer(), false);
    int end = original(span.at(1).integer(), true);
    // ERG can discard a unary sign as punctuation and start the card span at
    // the first digit. Keep the sign and its original offset as literal
    // evidence.
    if (ep["predicate"].text() == "card") {
      int pos = input.moveIndex32(0, begin);
      auto is_sign = [](UChar32 ch) {
        return ch == '-' || ch == 0x2212 || ch == '+';
      };
      UChar32 sign = pos < input.length() ? input.char32At(pos) : 0;
      if (!is_sign(sign)) {
        int before = pos;
        while (before > 0 &&
               u_isUWhiteSpace(input.char32At(input.moveIndex32(before, -1))))
          before = input.moveIndex32(before, -1);
        if (before > 0) {
          int previous = input.moveIndex32(before, -1);
          sign = input.char32At(previous);
          if (is_sign(sign))
            begin = input.countChar32(0, previous);
        }
      }
      if (is_sign(sign))
        ep.set("literal_sign", gui::str(sign == '+' ? "+" : "-"));
    }
    ep.set("source_span", gui::arr({Json::number(begin), Json::number(end)}));
    if (!ep["predicate"].text().starts_with("_"))
      continue;
    for (const auto &a : edits)
      if (span.at(0).integer() >= a["start"].integer() &&
          span.at(1).integer() <= a["end"].integer()) {
        ep.set("lexical_concept", a["concept"]);
        ep.set("lexical_kind", a["kind"]);
        ep.set("source_term", a["source"]);
        ep.set("source_span", a["source_span"]);
        break;
      }
  }
}
} // namespace unllm::native
