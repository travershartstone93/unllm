#include "unllm/maintenance/evaluate.h"
#include "../symbolic/gui_json.h"
#include "unllm/symbolic/process.h"
#define PCRE2_CODE_UNIT_WIDTH 8
#include "delimited.h"
#include <deque>
#include <future>
#include <iomanip>
#include <map>
#include <pcre2.h>
#include <set>
#include <sstream>
#include <unicode/unistr.h>
namespace unllm::maintenance {
using namespace native;
using namespace native::gui;
namespace {
bool match(const std::string &pattern, const std::string &answer) {
  int error;
  PCRE2_SIZE offset;
  std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)> code(
      pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()),
                    pattern.size(), PCRE2_CASELESS | PCRE2_UTF | PCRE2_UCP,
                    &error, &offset, nullptr),
      pcre2_code_free);
  if (!code)
    throw Unsupported("Invalid evaluation pattern at byte " +
                      std::to_string(offset));
  std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)> data(
      pcre2_match_data_create_from_pattern(code.get(), nullptr),
      pcre2_match_data_free);
  auto result =
      pcre2_match(code.get(), reinterpret_cast<PCRE2_SPTR>(answer.data()),
                  answer.size(), 0, 0, data.get(), nullptr);
  if (result == PCRE2_ERROR_NOMATCH)
    return false;
  if (result < 0)
    throw Unsupported("Evaluation pattern failed: " + std::to_string(result));
  return true;
}
std::string percent(int a, int b) {
  if (!b)
    return "n/a";
  std::ostringstream out;
  out << std::fixed << std::setprecision(0) << 100. * a / b << '%';
  return out.str();
}
std::string trim(const std::string &s) {
  auto a = s.find_first_not_of(" \t\r\n");
  return a == s.npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
} // namespace
std::vector<Json> read_questions(const Path &file) {
  auto rows = parse_delimited(read_file(file), '\t', true);
  if (rows.empty())
    throw Unsupported("Missing TSV header");
  std::map<std::string, size_t> columns;
  for (size_t j = 0; j < rows.front().size(); ++j)
    if (!columns.emplace(rows.front()[j], j).second)
      throw Unsupported("Duplicate TSV column");
  if (!columns.contains("question"))
    throw Unsupported("Missing TSV question column");
  std::vector<Json> result;
  std::set<std::string> ids;
  for (size_t i = 1; i < rows.size(); ++i) {
    auto field = [&](const std::string &k) {
      auto it = columns.find(k);
      return it != columns.end() && it->second < rows[i].size()
                 ? rows[i][it->second]
                 : "";
    };
    auto text = field("question"), id = field("id");
    if (text.empty() || text.find('\0') != text.npos)
      throw Unsupported("Empty or invalid evaluation question");
    if (id.empty()) {
      std::ostringstream out;
      out << 'q' << std::setw(3) << std::setfill('0') << i - 1;
      id = out.str();
    }
    if (!ids.insert(id).second)
      throw Unsupported("Duplicate evaluation question ID");
    auto pattern = field("answer_pattern");
    if (pattern.empty())
      pattern = field("expect");
    bool unknown =
        field("category") == "limits" ||
        (pattern.empty() && (!columns.contains("expect") ||
                             columns.at("expect") >= rows[i].size()));
    result.push_back(obj({{"id", str(id)},
                          {"question", str(text)},
                          {"pattern", str(pattern)},
                          {"unknowable", boolean(unknown)}}));
  }
  return result;
}
Json judge(const Json &question, const std::string &branch,
           const std::string &answer) {
  bool abstain = answer.empty() || branch == "none" ||
                 branch.starts_with("category:") ||
                 answer.starts_with("I couldn't find that") ||
                 answer.starts_with("No reference packs are installed") ||
                 answer.starts_with("That is a question about this machine");
  std::string kind = abstain ? "abstained"
                     : answer.starts_with("The closest thing I know")
                         ? "hedged"
                         : "answered";
  bool unknown = question["unknowable"].flag(),
       right = unknown
                   ? abstain
                   : (!abstain && match(question["pattern"].text(), answer));
  auto snippet = icu::UnicodeString::fromUTF8(answer);
  snippet.truncate(snippet.moveIndex32(0, 400));
  std::string utf8;
  snippet.toUTF8String(utf8);
  return obj({{"id", question["id"]},
              {"question", question["question"]},
              {"kind", str(kind)},
              {"right", boolean(right)},
              {"unknowable", boolean(unknown)},
              {"branch", str(branch)},
              {"answer", str(utf8)}});
}
std::pair<std::string, std::string> ask_candidate(const Path &cli,
                                                  const std::string &question, const std::vector<std::string>& env) {
  std::vector<std::string> command;
  if (!env.empty()) { command.push_back("/usr/bin/env"); command.insert(command.end(),env.begin(),env.end()); }
  command.insert(command.end(),{cli.string(), "--branch", "--brief", "--passages", "20","--", question});
  auto r = Process(command)
               .finish(after_ms(180000));
  if (r.exit_code)
    throw Unsupported("Candidate process exited " +
                      std::to_string(r.exit_code));
  std::istringstream input(r.out);
  std::string line, answer, branch;
  while (std::getline(input, line)) {
    if (line.starts_with("[branch: ") && line.ends_with("]"))
      branch = line.substr(9, line.size() - 10);
    else if (!line.starts_with("[brief] ") && !line.starts_with("[passage] ") &&
             branch.empty() && !line.starts_with("  ")) {
      if (!answer.empty())
        answer += ' ';
      answer += line;
    }
  }
  if (branch.empty() && trim(answer).empty())
    throw Unsupported("Candidate produced no answer record");
  return {branch, trim(answer)};
}
void Evaluation::observe(const Json &r) {
  auto k = r["kind"].text();
  bool good = r["right"].flag();
  if (r["unknowable"].flag()) {
    ++unknowable;
    declined_unknown += k == "abstained";
    return;
  }
  ++known;
  answered += k == "answered";
  hedged += k == "hedged";
  right += good;
  confident_right += good && k == "answered";
}
Json Evaluation::data() const {
  return obj({{"known", Json::number(known)},
              {"answered", Json::number(answered)},
              {"hedged", Json::number(hedged)},
              {"right", Json::number(right)},
              {"confident_right", Json::number(confident_right)},
              {"unknowable", Json::number(unknowable)},
              {"declined_unknown", Json::number(declined_unknown)}});
}
std::string Evaluation::summary(const std::string &name) const {
  std::ostringstream out;
  auto replied = answered + hedged;
  if (known)
    out << name << ": " << known << " questions\n  right " << right << " ("
        << percent(right, known) << " of all)   answered " << answered
        << "   hedged " << hedged << "   declined " << known - replied
        << "\n  precision " << percent(right, replied)
        << " (right / replied)   confident precision "
        << percent(confident_right, answered)
        << " (hedged left out)   coverage " << percent(replied, known) << '\n';
  if (unknowable)
    out << name << ": " << unknowable << " unknowable questions, declined "
        << declined_unknown << ", made up " << unknowable - declined_unknown
        << '\n';
  return out.str();
}
Evaluation evaluate_tsv(const Path &input, const Answer &answer,
                        const std::function<void(const Json &)> &record,
                        unsigned workers, const Json &completed) {
  if (!workers || workers > 128)
    throw Unsupported("Invalid evaluation worker count");
  auto tasks = read_questions(input);
  auto name = input.stem().string();
  bool private_set =
      name.find("sealed") != name.npos || name.find("heldout") != name.npos;
  std::deque<std::future<Json>> pending;
  Evaluation totals;
  auto collect = [&] {
    auto row = pending.front().get();
    pending.pop_front();
    totals.observe(row);
    if (record && !private_set)
      record(row);
  };
  for (auto &q : tasks) {
    if (!private_set && completed.contains(q["id"].text())) {
      const auto previous = completed[q["id"].text()];
      if (previous["question"].text() != q["question"].text() ||
          !previous["right"].is_boolean() ||
          previous["unknowable"].flag() != q["unknowable"].flag())
        throw Unsupported("Invalid evaluation checkpoint");
      totals.observe(previous);
      continue;
    }
    auto one = [&, q] {
      auto [branch, text] = answer(q["question"].text());
      return judge(q, branch, text);
    };
    pending.push_back(std::async(
        workers > 1 ? std::launch::async : std::launch::deferred, one));
    if (pending.size() >= workers)
      collect();
  }
  while (!pending.empty())
    collect();
  return totals;
}
} // namespace unllm::maintenance
