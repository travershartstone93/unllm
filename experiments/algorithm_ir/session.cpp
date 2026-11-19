#include "session.h"
#include "search.h"
#include "unllm/maintenance/sources.h"
#include "unllm/symbolic/gui_target.h"
#include "validate.h"
#include <fcntl.h>
#include <set>
#include <sys/file.h>
#include <unistd.h>
namespace unllm::algorithm {
namespace fs = std::filesystem;
using maintenance::write_atomic;
using namespace native;
namespace {
struct Lock {
  int fd;
  explicit Lock(const fs::path &p)
      : fd(open(p.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC)) {
    if (fd < 0)
      throw Unsupported("Cannot open session");
    if (flock(fd, LOCK_EX) < 0) {
      close(fd);
      throw Unsupported("Cannot lock session");
    }
  }
  ~Lock() { close(fd); }
};
Json edit(const Json &old, const Json &request) {
  fields_only(request,
              {"examples", "withdraw", "replace", "edits", "base_revision"});
  std::map<std::string, Json> kept;
  std::vector<std::string> order;
  for (auto row : old["examples"].items()) {
    auto id = digest(row["input"]);
    if (!kept.contains(id))
      order.push_back(id);
    kept[id] = row;
  }
  std::set<std::string> touched;
  auto edit_id = [&](Json j) {
    auto id = j.text();
    if (!kept.contains(id) || !touched.insert(id).second)
      throw Unsupported("Unknown or duplicate edited requirement");
    return id;
  };
  for (auto key : {"withdraw", "replace", "examples"})
    if (request.contains(key) && !request[key].is_array())
      throw Unsupported("Revision lists must be arrays");
  for (auto j : request["withdraw"].items())
    kept.erase(edit_id(j));
  for (auto r : request["replace"].items()) {
    fields_only(r, {"id", "output"});
    if (!r.contains("output"))
      throw Unsupported("Replacement needs an output");
    kept.at(edit_id(r["id"])).set("output", r["output"]);
  }
  auto next = old.clone(), rows = Json::array();
  for (auto &id : order)
    if (kept.contains(id))
      rows.push(kept.at(id));
  for (auto r : request["examples"].items())
    rows.push(r);
  next.set("examples", rows);
  return next;
}
Json replace_component(Json root, const Json &path, size_t depth,
                       const Json &change) {
  if (depth == path.size()) {
    if (digest(root) != change["before"].text())
      throw Unsupported("Component precondition does not match");
    return change["value"];
  }
  auto part = path.at(depth);
  if (part.is_string() && root.contains(part.text())) {
    root.set(part.text(),
             replace_component(root[part.text()], path, depth + 1, change));
    return root;
  }
  if (part.is_integer() && part.integer() >= 0 && root.is_array() &&
      size_t(part.integer()) < root.size()) {
    auto out = Json::array();
    for (size_t i = 0; i < root.size(); ++i)
      out.push(i == size_t(part.integer())
                   ? replace_component(root.at(i), path, depth + 1, change)
                   : root.at(i));
    return out;
  }
  throw Unsupported("Invalid typed component path");
}
} // namespace
Json check(const Json &ir, const Json &contract, const fs::path &job,
           const std::string &python) {
  if (python.empty())
    throw Unsupported("Specify an interpreter for generated Python validation");
  auto p = decode(ir);
  auto &f = p.functions.back();
  if (f.result == Type::boolean || f.parameters != 1 ||
      f.slots[0] != Type::sequence || contract["input"].text() != "ints" ||
      contract["output"].text() != (f.result == Type::integer ? "int" : "ints"))
    throw Unsupported("Validation signature mismatch");
  auto examples = contract["examples"];
  if (!examples.is_array() || !examples.size() || examples.size() > 128)
    throw Unsupported("Expected 1 to 128 validation examples");
  std::vector<Json> cases;
  for (auto e : examples.items()) {
    auto input = argument(e["input"], Type::sequence),
         output = argument(e["output"], f.result);
    if (interpret(p, {input}) != output)
      throw Unsupported("Interpreter disagrees with example " +
                        std::to_string(cases.size()));
    auto row = Json::object(), args = Json::array();
    args.push(json(input));
    row.set("args", args);
    row.set("expected", json(output));
    cases.push_back(row);
  }
  auto report = Json::object();
  report.set("examples", Json::number(cases.size()));
  for (auto lang : {"cpp", "python"}) {
    bool py = std::string_view(lang) == "python";
    auto src = emit(p, py);
    write_atomic(job / (py ? "algorithm.py" : "algorithm.cpp"), src);
    check_emission(job, src, cases, py ? python : "");
    report.set(lang, Json::string(sha256(src)));
  }
  return report;
}
Json construct(const fs::path &session, const Json &request, bool revision,
               const std::string &python) {
  fs::create_directories(session);
  Lock lock(session);
  auto current = session / "current.json";
  bool exists = fs::exists(current);
  if (exists != revision)
    throw Unsupported(exists ? "Session already exists"
                             : "Session has no accepted revision");
  auto head = exists ? Json::parse(read_file(current)) : Json::object();
  auto attempt = fresh_gui_directory(session, "attempt");
  write_atomic(attempt / "request.json", canonical(request));
  auto r = Json::object();
  try {
    auto contract =
        revision ? edit(head["contract"].clone(), request) : request;
    if (request.contains("base_revision") &&
        (!request["base_revision"].is_integer() ||
         request["base_revision"].integer() != head["revision"].integer()))
      throw Unsupported("Stale revision");
    if (revision && request.contains("edits")) {
      auto changes = request["edits"];
      if (!request.contains("base_revision") || !changes.is_array() ||
          changes.size() == 0 || changes.size() > 32)
        throw Unsupported("Expected base revision and 1 to 32 edits");
      auto proposed = head["ir"].clone();
      for (auto change : changes.items()) {
        fields_only(change, {"path", "before", "value"});
        auto path = change["path"];
        if (!path.is_array() || path.size() > 80 || !change.contains("value"))
          throw Unsupported("Malformed edit");
        proposed = replace_component(proposed, path, 0, change);
      }
      decode(proposed);
      r.set("ir", proposed);
      r.set("ir_sha256", Json::string(digest(proposed)));
      r.set("status", Json::string("candidate"));
      r.set("method", Json::string("explicit_edit"));
    } else
      r = synthesize(contract, head["ir"]);
    r.set("contract", contract);
    r.set("parent", Json::number(head["revision"].integer()));
    if (r["status"].text() == "candidate") {
      r.set("status", Json::string("validating"));
      auto begin = std::chrono::steady_clock::now();
      auto validation = check(r["ir"], contract, attempt, python);
      validation.set("ms",
                     Json::real(std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - begin)
                                    .count()));
      r.set("validation", validation);
      r.set("status", Json::string("examples_validated"));
      r.set("revision", Json::number(head["revision"].integer() + 1));
      auto ids = Json::array();
      for (auto row : contract["examples"].items())
        ids.push(Json::string(digest(row["input"])));
      r.set("requirement_ids", ids);
    }
  } catch (const std::exception &e) {
    r.set("status", Json::string(r["status"].text() == "validating"
                                     ? "validation_failed"
                                     : "invalid_revision"));
    r.set("reason", Json::string(e.what()));
  }
  r.set("attempt", Json::string(attempt.filename().string()));
  write_atomic(attempt / "result.json", canonical(r));
  if (r["status"].text() == "examples_validated")
    write_atomic(current, canonical(r));
  return r;
}
} // namespace unllm::algorithm
