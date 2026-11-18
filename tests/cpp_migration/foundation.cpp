#include "unllm/symbolic/common.h"
#include "unllm/symbolic/process.h"
#include <cerrno>
#include <cvc5/cvc5.h>
#include <gmp.h>
#include <iostream>
#include <sys/wait.h>
using namespace unllm::native;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  try {
    check(std::string(gmp_version) ==
              std::to_string(__GNU_MP_VERSION) + "." +
                  std::to_string(__GNU_MP_VERSION_MINOR) + "." +
                  std::to_string(__GNU_MP_VERSION_PATCHLEVEL),
          "Native GMP SDK/header version agreement");
    check(key("  ＡＢＣ\tStraße\nΣς  ") == "abc strasse σσ", "Unicode keys");
    bool invalid = false;
    try {
      key(std::string("\xff"));
    } catch (const Unsupported &) {
      invalid = true;
    }
    check(invalid, "invalid UTF8");
    check(
        sha256("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA256");
    check(canonical(Json::parse(
              "{\"z\":-0.0,\"a/b\":[1e-6,1e16,0.0001,100.0,\"/é\"]}")) ==
              "{\"a/b\":[1e-06,1e+16,0.0001,100.0,\"/é\"],\"z\":-0.0}",
          "canonical floats/strings");
    check(Json::parse("1").is_integer(), "scalar JSON");
    check(Json::parse("true").is_boolean(), "Boolean distinction");
    invalid = false;
    try {
      Json::parse("{} trailing");
    } catch (const std::exception &) {
      invalid = true;
    }
    check(invalid, "trailing JSON rejection");
    {
      Process p({"/usr/bin/cat"});
      p.write("first\nsecond\n", after_ms(1000));
      check(p.line(after_ms(1000)) == "first", "persistent line1");
      check(p.line(after_ms(1000)) == "second", "persistent line2");
      check(p.finish(after_ms(1000)).exit_code == 0, "child exit");
    }
    {
      Process p({"/bin/sh", "-c", "head -c 100000 /dev/zero >&2; printf done"});
      auto r = p.finish(after_ms(2000));
      check(r.out == "done" && r.err.size() == 100000,
            "concurrent stderr drain");
    }
    {
      Process p({"/bin/sleep", "30"});
      auto pid = p.pid();
      invalid = false;
      try {
        p.finish(after_ms(20));
      } catch (const ResourceLimit &) {
        invalid = true;
      }
      check(invalid, "timeout");
      check(waitpid(pid, nullptr, WNOHANG) == -1 && errno == ECHILD,
            "reaped timeout child");
    }
    {
      Process p({"/usr/bin/head", "-c", "4096", "/dev/zero"}, "", 1024);
      invalid = false;
      try {
        p.finish(after_ms(1000));
      } catch (const ResourceLimit &) {
        invalid = true;
      }
      check(invalid, "output limit");
    }
    cvc5::TermManager tm;
    cvc5::Solver solver(tm);
    solver.setOption("sygus", "true");
    solver.setOption("incremental", "false");
    solver.setLogic("LIA");
    auto integer = tm.getIntegerSort();
    auto x = tm.mkVar(integer, "x"), start = tm.mkVar(integer, "Start");
    auto g = solver.mkGrammar({x}, {start});
    g.addRules(start, {x, tm.mkInteger(11),
                       tm.mkTerm(cvc5::Kind::SUB, {start, start})});
    auto f = solver.synthFun("decrement", {x}, integer, g);
    auto input = solver.declareSygusVar("input", integer);
    solver.addSygusConstraint(
        tm.mkTerm(cvc5::Kind::EQUAL,
                  {tm.mkTerm(cvc5::Kind::APPLY_UF, {f, input}),
                   tm.mkTerm(cvc5::Kind::SUB, {input, tm.mkInteger(11)})}));
    check(solver.checkSynth().hasSolution(), "native cvc5 synthesis");
    std::cout << "Native foundation checks passed; cvc5 solution: "
              << solver.getSynthSolution(f) << "\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
