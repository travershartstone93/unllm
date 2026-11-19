#include "gui_json.h"
#include "unllm/symbolic/behaviour_job.h"
#include <algorithm>
#include <charconv>
namespace unllm::native {
namespace {
void require(bool ok, const std::string &why) {
  if (!ok)
    throw Unsupported(why);
}
bool matches(const Json &a, const Json &b) {
  if (a.is_number() && b.is_number()) {
    auto x = a.real(), y = b.real();
    return std::abs(x - y) <=
           std::max(1e-9, 1e-10 * std::max(std::abs(x), std::abs(y)));
  }
  if (a.type() != b.type() || a.size() != b.size())
    return false;
  if (a.is_object()) {
    for (auto &[k, v] : b.members())
      if (!a.contains(k) || !matches(a[k], v))
        return false;
    return true;
  }
  if (a.is_array()) {
    for (size_t i = 0; i < a.size(); ++i)
      if (!matches(a.at(i), b.at(i)))
        return false;
    return true;
  }
  return canonical(a) == canonical(b);
}
bool holds(const BehaviourValue &v, const Json &env) {
  if (v.kind == "binary") {
    if (v.name == "and")
      return holds(v.args[0], env) && holds(v.args[1], env);
    if (v.name == "or")
      return holds(v.args[0], env) || holds(v.args[1], env);
    if (v.name == "eq" || v.name == "ne")
      return matches(evaluate_behaviour(v.args[0], env),
                     evaluate_behaviour(v.args[1], env)) == (v.name == "eq");
  }
  return evaluate_behaviour(v, env).flag();
}
void modified(const BehaviourValue &v, std::set<std::string> &names) {
  if (v.kind == "next")
    names.insert(v.name);
  for (auto &a : v.args)
    modified(a, names);
}
std::string display(const Json &v) {
  if (v.is_string())
    return v.text();
  if (v.is_number()) {
    char buf[64];
    auto [end, ec] =
        std::to_chars(buf, buf + sizeof(buf), v.real() == 0 ? 0.0 : v.real(),
                      std::chars_format::general, 12);
    if (ec != std::errc{})
      throw Unsupported("Display number conversion failed");
    return {buf, end};
  }
  return canonical(v);
}
} // namespace
void check_behaviour_outcome(const BehaviourSpec &spec, const Json &scenario,
                             const Json &actual,
                             const std::filesystem::path &output) {
  auto fields = spec.state(), state = actual["state"], exp = scenario["expect"];
  require(state.is_object() && state.size() == fields.size() &&
              actual["labels"].is_object() && actual["error"].is_string(),
          "Incomplete Qt observation");
  for (auto &[n, _] : fields.members())
    require(state.contains(n), "Missing observed state " + n);
  if (scenario.contains("expect"))
    fields_only(exp,
                {"state", "labels", "paths", "dirty", "visible", "error",
                 "has_error", "files", "trace_ranges", "unchanged_between"});
  for (auto k : {"state", "labels", "paths"})
    if (exp.contains(k)) {
      require(exp[k].is_object(), "Invalid expectation for " + std::string(k));
      for (auto &[n, v] : exp[k].members())
        require(actual[k].contains(n) && matches(actual[k][n], v),
                "Outcome mismatch for " + std::string(k) + "." + n);
    }
  for (auto k : {"dirty", "visible", "error"})
    if (exp.contains(k))
      require(actual.contains(k) && matches(actual[k], exp[k]),
              "Outcome mismatch for " + std::string(k));
  if (exp.contains("has_error"))
    require(exp["has_error"].is_boolean() &&
                exp["has_error"].flag() == !actual["error"].text().empty(),
            "Unexpected error state");
  if (scenario.contains("postcondition")) {
    auto before = Json::object();
    for (auto &[n, f] : fields.members())
      before.set(n, f["initial"]);
    for (auto &[n, v] : scenario["initial"].members())
      before.set(n, v);
    auto e = behaviour_expression(scenario["postcondition"], fields, true);
    require(e.type == "bool" && actual["error"].text().empty() &&
                holds(e.value, gui::obj({{"prev", before}, {"next", state}})),
            "Transition postcondition failed");
    std::set<std::string> updates;
    modified(e.value, updates);
    for (auto &[n, v] : before.members())
      if (!updates.contains(n))
        require(matches(state[n], v), "Unmentioned state changed: " + n);
  }
  if (exp.contains("files"))
    require(exp["files"].is_object(), "Invalid file expectations");
  for (auto &[name, value] : exp["files"].members()) {
    auto p = std::filesystem::path(name);
    require(p.parent_path() == "/output" && p.filename() != "." &&
                p.filename() != "..",
            "Expected file must be under /output");
    auto f = output / p.filename();
    require(
        std::filesystem::is_regular_file(std::filesystem::symlink_status(f)),
        "Missing regular output file: " + name);
    auto content = read_file(f, 8 * 1024 * 1024);
    require(matches(value.is_string() ? Json::string(content)
                                      : Json::parse(content),
                    value),
            "Saved contents differ: " + name);
  }
  for (auto key : {"trace_ranges", "unchanged_between"})
    if (exp.contains(key))
      require(exp[key].is_array(), "Invalid trace expectations");
  for (auto t : exp["trace_ranges"].items()) {
    auto value =
        actual["trace"].at(t["at"].integer())[t["state"].text()].real();
    require(value >= t["minimum"].real() && value <= t["maximum"].real(),
            "Timed observation outside requested bounds");
  }
  for (auto t : exp["unchanged_between"].items()) {
    auto n = t.at(2).text();
    auto before = actual["trace"].at(t.at(0).integer()),
         after = actual["trace"].at(t.at(1).integer());
    require(before.contains(n) && after.contains(n) &&
                matches(before[n], after[n]),
            "Stopped state continued changing");
  }
  for (auto v : spec.views().items())
    if (v["kind"].text() == "label") {
      auto label = v["label"].text();
      std::string wanted;
      try {
        wanted = (label.empty() ? "" : label + ": ") +
                 display(evaluate_behaviour(
                     behaviour_expression(v["value"], fields).value, state));
      } catch (const Unsupported &) {
        wanted = "Error";
      }
      auto id = v["id"].text();
      require(actual["labels"][id].is_string() &&
                  actual["labels"][id].text() == wanted,
              "Derived label differs: " + id + " expected " + wanted +
                  " observed " + actual["labels"][id].dump());
    }
}
} // namespace unllm::native
