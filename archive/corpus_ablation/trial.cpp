#include "search.h"
#include "signature.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <tuple>
using namespace unllm::algorithm;
using namespace trial;
using namespace unllm::native;
namespace {
struct Expr {
  Signature sig;
  Json ir;
  int size;
  std::string text;
};
Json list(std::initializer_list<Json> parts) {
  auto v = Json::array();
  for (auto &a : parts)
    v.push(a);
  return v;
}
Expr atom(bool is_var, int n = 0) {
  auto ir = list({Json::string(is_var ? "var" : "int"),
                  Json::string(is_var ? "element" : std::to_string(n))});
  return {is_var ? variable : constant(n), ir, 1, ir.dump()};
}
Expr apply(char op, const Expr &a, const Expr &b) {
  auto ir = list({Json::string(op == '+'   ? "add"
                               : op == '-' ? "sub"
                                           : "mul"),
                  a.ir, b.ir});
  return {combine(op, a.sig, b.sig), ir, 1 + a.size + b.size, ir.dump()};
}
std::map<Signature, Expr> universe() {
  auto x = atom(true);
  std::vector<Expr> atoms{x}, first;
  for (int n = -12; n <= 12; ++n)
    atoms.push_back(atom(false, n));
  std::map<Signature, Expr> pool;
  auto add = [&](Expr e) {
    if (e.sig == constant(e.sig[0]))
      return;
    auto it = pool.find(e.sig);
    if (it == pool.end() ||
        std::tie(e.size, e.text) < std::tie(it->second.size, it->second.text))
      pool.insert_or_assign(e.sig, e);
  };
  add(x);
  for (auto &a : atoms)
    for (char op : std::string("+-*")) {
      auto e = apply(op, x, a);
      add(e);
      first.push_back(e);
      e = apply(op, a, x);
      add(e);
      first.push_back(e);
    }
  for (auto &e : first)
    for (char op : std::string("+-*")) {
      add(apply(op, e, e));
      for (auto &a : atoms) {
        add(apply(op, e, a));
        add(apply(op, a, e));
      }
    }
  return pool;
}
int evaluate(const Json &ir, int x) {
  auto op = ir.at(0).text();
  if (op == "var")
    return x;
  if (op == "int")
    return std::stoi(ir.at(1).text());
  int a = evaluate(ir.at(1), x), b = evaluate(ir.at(2), x);
  return op == "add" ? a + b : op == "sub" ? a - b : a * b;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("usage: trial stock|none|INDEX REQUESTS");
    std::string arm = argv[1];
    bool transitions = arm.starts_with("state:");
    if (transitions)
      arm.erase(0, 6);
    std::vector<std::string> operations;
    if (transitions && arm == "none")
      operations = {"add", "sub", "min", "max"};
    auto requests = Json::parse(read_file(argv[2]));
    auto setup = std::chrono::steady_clock::now();
    bool reuse = arm.starts_with("reuse:");
    auto pool =
        (arm == "stock" || reuse) ? std::map<Signature, Expr>() : universe();
    std::map<Signature, int> frequency;
    if (arm != "stock" && arm != "none") {
      auto index =
          Json::parse(read_file(reuse                         ? arm.substr(6)
                                : arm.starts_with("placebo:") ? arm.substr(8)
                                                              : arm));
      if (transitions) {
        if (!index.contains("transition_operations"))
          throw std::runtime_error("missing transition evidence");
        auto evidence = index["transition_operations"];
        for (auto &[op, record] : evidence.members())
          operations.push_back(op);
        std::sort(operations.begin(), operations.end(), [&](auto &a, auto &b) {
          auto count = [&](auto &k) {
            return evidence[k]["documents"].integer();
          };
          return count(a) != count(b) ? count(a) > count(b) : a < b;
        });
      }
      for (auto &[stored, entry] : index["sources"].members()) {
        auto ir = entry["ir"];
        Signature sig;
        for (int x = -2; x <= 2; ++x)
          sig[x + 2] = evaluate(ir, x);
        if (key(sig) != stored)
          throw std::runtime_error("source AST signature mismatch");
        if (reuse) {
          std::function<int(const Json &)> nodes = [&](const Json &n) {
            return n.size() == 2 ? 1 : 1 + nodes(n.at(1)) + nodes(n.at(2));
          };
          pool.emplace(sig, Expr{sig, ir, nodes(ir), ir.dump()});
        }
      }
      for (auto &[sig, e] : pool)
        frequency[sig] = index["df"][key(sig)].integer();
    }
    for (auto &[sig, e] : pool)
      for (int x = -2; x <= 2; ++x)
        if (evaluate(e.ir, x) != sig[x + 2])
          throw std::runtime_error("proposal AST signature mismatch");
    if (arm.starts_with("placebo:")) {
      std::vector<std::pair<std::string, Signature>> order;
      std::vector<int> values;
      for (auto &[sig, count] : frequency) {
        order.emplace_back(sha256("corpus-trial-17/" + key(sig)), sig);
        values.push_back(count);
      }
      std::sort(order.begin(), order.end());
      for (size_t i = 0; i < order.size(); ++i)
        frequency[order[i].second] = values[i];
    }
    auto setup_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - setup)
                        .count();
    for (auto &[id, contract] : requests.members()) {
      Json shortlist = Json::array();
      auto rank = [&](const Json &input) {
        std::vector<std::pair<int, int>> probes;
        for (auto row : input["examples"].items()) {
          if (row["input"].size() != 1)
            continue;
          auto y = row["output"];
          if (y.is_array()) {
            if (y.size() != 1)
              continue;
            y = y.at(0);
          }
          int x = std::stoi(row["input"].at(0).text());
          if (x >= -32 && x <= 32)
            probes.emplace_back(x, std::stoi(y.text()));
        }
        using Rank = std::tuple<int, int, int, std::string>;
        std::vector<std::pair<Rank, const Expr *>> scores;
        for (auto &[sig, e] : pool) {
          int matches = 0;
          for (auto [x, y] : probes)
            matches += evaluate(e.ir, x) == y;
          scores.emplace_back(Rank{-matches, -frequency[sig], e.size, e.text},
                              &e);
        }
        std::sort(scores.begin(), scores.end(),
                  [](auto &a, auto &b) { return a.first < b.first; });
        std::vector<Json> proposals;
        for (size_t i = 0; i < 32 && i < scores.size(); ++i) {
          proposals.push_back(scores[i].second->ir);
          shortlist.push(Json::string(key(scores[i].second->sig)));
        }
        return proposals;
      };
      auto start = std::chrono::steady_clock::now();
      auto r = arm == "stock"
                   ? synthesize(contract)
                   : synthesize_with_fragments(contract, rank, operations);
      r.erase("counterexamples");
      r.set("task", Json::string(id));
      r.set("pool", Json::number(pool.size()));
      r.set("setup_ms", Json::real(setup_ms));
      r.set("shortlist", shortlist);
      r.set("online_ms",
            Json::real(std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start)
                           .count()));
      std::cout << r.dump() << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
