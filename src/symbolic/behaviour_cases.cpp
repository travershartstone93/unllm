#include "gui_json.h"
#include "unllm/symbolic/behaviour_job.h"
#include <algorithm>
#include <random>
namespace unllm::native {
using namespace gui;
namespace {
Json sample(const Json &field) {
  if (field["type"].text() == "text")
    return str("Alpha café\nbeta Alpha\n");
  auto row = Json::object();
  for (auto &[key, kind] : field["fields"].members())
    row.set(key, kind.text() == "text"   ? str("Alpha café")
                 : kind.text() == "bool" ? boolean(false)
                                         : num(1.25));
  return arr({row});
}
Json row(const std::string &name, Json events, Json expected,
         Json seed = Json::object(), Json dialogs = Json::object()) {
  return obj({{"name", str(name)},
              {"initial", seed},
              {"events", events},
              {"expect", expected},
              {"dialogs", dialogs}});
}
Json state_expect(const Json &state) { return obj({{"state", state}}); }
Json event(const std::string &kind, const Json &value) {
  return obj({{kind, value}});
}
void file_cases(Json &cases, const Json &a, const Json &field,
                const Json &initial, const std::filesystem::path &job) {
  auto id = a["id"].text(), target = a["state"].text(),
       effect = a["effect"].text();
  auto click = arr({event("button", a["id"])}), content = sample(field);
  auto seeded = with(initial, target, content);
  if (effect == "save" || effect == "save_as") {
    auto path = str("/output/save-" + id + ".txt");
    auto expected = obj({{"state", seeded},
                         {"files", obj({{path.text(), content}})},
                         {"dirty", boolean(false)},
                         {"error", str("")},
                         {"paths", obj({{target, path}})}});
    cases.push(row("save-" + id, click, expected, seeded,
                   obj({{"save", arr({path})}})));
    cases.push(row("cancel-" + id, click,
                   obj({{"state", seeded}, {"error", str("")}}), seeded,
                   obj({{"save", arr({str("")})}})));
    cases.push(row("failed-" + id, click,
                   obj({{"state", seeded},
                        {"has_error", boolean(true)},
                        {"dirty", boolean(true)}}),
                   seeded,
                   obj({{"save", arr({str("/output/missing/file.txt")})}})));
  } else if (effect == "load") {
    auto good = "fixture-" + id + ".txt", bad = "invalid-" + id + ".txt";
    write(job / good,
          content.is_string() ? content.text() : canonical(content));
    write(job / bad, std::string(2, char(0xff)));
    cases.push(row(
        "open-" + id, click,
        obj({{"state", seeded}, {"dirty", boolean(false)}, {"error", str("")}}),
        {}, obj({{"open", arr({str("/job/" + good)})}})));
    cases.push(row("bad-" + id, click,
                   obj({{"state", initial}, {"has_error", boolean(true)}}), {},
                   obj({{"open", arr({str("/job/" + bad)})}})));
    cases.push(row("cancel-" + id, click,
                   obj({{"state", initial}, {"error", str("")}}), {},
                   obj({{"open", arr({str("")})}})));
  } else
    cases.push(
        row("new-" + id, click,
            obj({{"state", with(initial, target,
                                content.is_string() ? str("") : Json::array())},
                 {"dirty", boolean(false)}}),
            seeded, obj({{"confirm", arr({str("discard")})}})));
}
void record_case(Json &cases, const Json &a, const Json &fields, Json seed) {
  auto target = a["state"].text(), effect = a["effect"].text(),
       sel = a["selection"].text();
  auto record = sample(fields[target]).at(0);
  if (effect != "append_record") {
    seed.set(target, arr({record, record}));
    seed.set(sel, num(0));
  }
  auto expected = seed.clone(), values = Json::object();
  for (auto &[k, source] : a["values"].members())
    values.set(k, evaluate_behaviour(behaviour_expression(source, fields).value,
                                     seed));
  if (effect == "append_record") {
    expected[target].push(values);
    if (!sel.empty())
      expected.set(sel, num(expected[target].size() - 1));
    for (auto name : a["clear"].items())
      expected.set(name.text(), str(""));
  } else if (effect == "remove_record") {
    expected.set(target, arr({record}));
    expected.set(sel, num(0));
  } else
    for (auto &[k, v] : values.members())
      expected[target].at(0).set(k, v);
  cases.push(row(effect + "-" + a["id"].text(), arr({event("button", a["id"])}),
                 obj({{"state", expected}, {"error", str("")}}), seed));
}
} // namespace
Json behaviour_cases(const BehaviourSpec &spec,
                     const std::filesystem::path &job) {
  auto fields = spec.state(), actions = spec.actions(),
       initial = Json::object(), cases = Json::array();
  for (auto &[n, f] : fields.members())
    initial.set(n, f["initial"]);
  cases.push(row("initial", Json::array(), state_expect(initial)));
  auto seeded = initial.clone();
  for (auto &[n, f] : fields.members())
    if (f["type"].text() == "text")
      seeded.set(n, sample(f));
  for (auto view : spec.views().items()) {
    auto kind = view["kind"].text(), id = view["id"].text(),
         target = view["state"].text();
    if (kind == "line_edit" || kind == "text_edit") {
      auto s = kind == "text_edit" ? sample(fields[target]) : str("Alpha café");
      cases.push(row("input-" + id,
                     arr({obj({{"input", str(id)}, {"text", s}})}),
                     state_expect(with(initial, target, s))));
    } else if (kind == "number_input") {
      for (auto v : {37.25, .25, -100.75, 1.0})
        cases.push(row(
            "number-" + id,
            arr({obj({{"number", str(id)},
                      {"value", num(v)},
                      {"text", str(v == 1.0 ? "1." : canonical(num(v)))}})}),
            state_expect(with(initial, target, num(v)))));
    } else if (kind == "record_list") {
      auto item = sample(fields[target]).at(0);
      auto seed = with(initial, target, arr({item, item}));
      auto sel = view["selection"].text();
      cases.push(row("select-" + id,
                     arr({obj({{"select", str(id)}, {"row", num(1)}})}),
                     state_expect(obj({{sel, num(1)}})), seed));
      if (view.contains("checked")) {
        auto changed =
            arr({with(item, view["checked"].text(), boolean(true)), item});
        cases.push(row("check-" + id,
                       arr({obj({{"check", str(id)},
                                 {"row", num(0)},
                                 {"value", boolean(true)}})}),
                       state_expect(obj({{target, changed}, {sel, num(0)}})),
                       seed));
      }
    }
  }
  std::set<std::string> persistent;
  for (auto a : actions.items()) {
    auto id = a["id"].text(), target = a["state"].text(),
         effect = a["effect"].text();
    auto click = arr({event("button", a["id"])});
    if (a.contains("ensure") || a.contains("assign")) {
      std::mt19937 rng(57017);
      for (int i = 0; i < (a.contains("ensure") ? 20 : 1); ++i) {
        auto seed = seeded.clone();
        if (a.contains("ensure"))
          for (auto &[n, f] : fields.members())
            if (f["type"].text() == "number")
              seed.set(n,
                       num(i == 0   ? 0
                           : i == 1 ? -1
                                    : static_cast<int>(rng() % 2001) - 1000));
        auto scenario =
            row(id + "-" + std::to_string(i), click, Json::object(), seed);
        bool guard =
            !a.contains("requires") ||
            evaluate_behaviour(
                behaviour_expression(a["requires"], fields).value, seed)
                .flag();
        if (!guard)
          scenario.set("expect",
                       obj({{"state", seed}, {"has_error", boolean(true)}}));
        else if (a.contains("ensure"))
          scenario.set("postcondition", a["ensure"]);
        else {
          auto expected = seed.clone();
          try {
            for (auto &[name, s] : a["assign"].members())
              expected.set(name,
                           evaluate_behaviour(
                               behaviour_expression(s, fields).value, seed));
            scenario.set("expect",
                         obj({{"state", expected}, {"error", str("")}}));
          } catch (const Unsupported &) {
            scenario.set("expect",
                         obj({{"state", seed}, {"has_error", boolean(true)}}));
          }
        }
        cases.push(scenario);
      }
    } else if (effect == "load" || effect == "save" || effect == "save_as" ||
               effect == "new") {
      file_cases(cases, a, fields[target], initial, job);
      persistent.insert(target);
    } else if (effect == "append_record" || effect == "set_record" ||
               effect == "remove_record") {
      record_case(cases, a, fields, seeded.clone());
    } else if (effect == "start_clock") {
      Json stop, reset, tick;
      for (auto b : actions.items())
        if (b["state"].text() == target &&
            b["running"].text() == a["running"].text() &&
            b["anchor"].text() == a["anchor"].text()) {
          if (b["effect"].text() == "stop_clock")
            stop = b["id"];
          if (b["effect"].text() == "reset_clock")
            reset = b["id"];
          if (b["effect"].text() == "sample_clock")
            tick = b["id"];
        }
      if (!stop.null() && !reset.null() && !tick.null()) {
        int64_t interval = 0;
        for (auto e : spec.data()["events"].items())
          if (e["action"].text() == tick.text())
            interval = e["interval_ms"].integer();
        if (interval && interval <= 800) {
          auto events =
              arr({event("button", a["id"]),
                   event("wait_ms", num(std::max<int64_t>(100, interval * 2))),
                   event("button", stop), event("wait_ms", num(80)),
                   event("button", reset)});
          auto expected = obj(
              {{"state",
                obj({{target, num(0)}, {a["running"].text(), boolean(false)}})},
               {"trace_ranges", arr({obj({{"at", num(1)},
                                          {"state", str(target)},
                                          {"minimum", num(.01)},
                                          {"maximum", num(5)}})})},
               {"unchanged_between",
                arr({arr({num(2), num(3), str(target)})})}});
          cases.push(with(row("clock-" + id, events, expected), "trace",
                          boolean(true)));
        }
      }
    }
    if (cases.size() > 192)
      throw ResourceLimit("GUI validation exceeds 192 scenarios");
  }
  for (auto &target : persistent) {
    auto seed = with(initial, target, sample(fields[target]));
    for (auto choice : {"cancel", "discard"})
      cases.push(
          row("close-" + std::string(choice) + "-" + target,
              arr({event("close", boolean(true))}),
              obj({{"visible", boolean(std::string(choice) == "cancel")}}),
              seed, obj({{"confirm", arr({str(choice)})}})));
  }
  for (auto c : spec.data()["checks"].items())
    cases.push(c);
  if (cases.size() > 192)
    throw ResourceLimit("GUI validation exceeds 192 scenarios");
  return cases;
}
} // namespace unllm::native
