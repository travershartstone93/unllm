#include "ir.h"
#include <limits>
#include <sstream>
namespace unllm::algorithm {
namespace {
const char *cpp_runtime = R"(#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
using I=std::int64_t;
using Seq=std::vector<I>;
namespace {
void tick(int &fuel){if(--fuel<0)throw std::runtime_error("work limit");}
I add(I a,I b){I r;if(__builtin_add_overflow(a,b,&r))throw std::overflow_error("integer overflow");return r;}
I sub(I a,I b){I r;if(__builtin_sub_overflow(a,b,&r))throw std::overflow_error("integer overflow");return r;}
I mul(I a,I b){I r;if(__builtin_mul_overflow(a,b,&r))throw std::overflow_error("integer overflow");return r;}
I length(const Seq &s){if(s.size()>64)throw std::length_error("sequence bound");return static_cast<I>(s.size());}
I at(const Seq &s,I i){if(i<0||i>=length(s))throw std::out_of_range("index bound");return s[i];}
Seq append(Seq s,I x){if(length(s)==64)throw std::length_error("sequence bound");s.push_back(x);return s;}
Seq indices(I n){if(n<0||n>64)throw std::length_error("range bound");Seq s;for(I i=0;i<n;++i)s.push_back(i);return s;}
)";
const char *py_runtime = R"(def tick(fuel):
    fuel[0] -= 1
    if fuel[0] < 0: raise ValueError("work limit")

def checked(n):
    if type(n) is not int: raise TypeError("argument type")
    if not -(1 << 63) <= n < (1 << 63): raise OverflowError("integer overflow")
    return n

def length(s):
    if len(s) > 64: raise ValueError("sequence bound")
    return len(s)

def at(s,i):
    if not 0 <= i < len(s): raise IndexError("index bound")
    return s[i]

def append(s,x):
    if len(s) >= 64: raise ValueError("sequence bound")
    return s + [x]

def indices(n):
    if not 0 <= n <= 64: raise ValueError("range bound")
    return range(n)

)";
std::string cpp_type(Type t) {
  return t == Type::integer ? "I" : t == Type::boolean ? "bool" : "Seq";
}
struct Emitter {
  const Program &program;
  bool py;
  size_t function_index = 0;
  std::ostringstream out;
  unsigned temporary = 0;
  void line(int level, const std::string &s) {
    out << std::string(level * 4, ' ') << s << '\n';
  }
  std::string symbol(std::string prefix, size_t i, const std::string &name) {
    auto result = prefix + std::to_string(i) + "_";
    for (char c : name)
      if (c != '_' || result.back() != '_')
        result += c;
    return result;
  }
  std::string slot(size_t i) {
    return symbol("v", i, program.functions[function_index].names[i]);
  }
  std::string helper(size_t i) {
    return symbol("fn", i, program.functions[i].name);
  }
  std::string expr(const Node &n, int level, bool operand = false) {
    std::string value;
    if (n.op == Op::literal) {
      if (n.type == Type::boolean)
        value = py ? (n.literal ? "True" : "False")
                   : (n.literal ? "true" : "false");
      else if (!py && n.literal == std::numeric_limits<int64_t>::min())
        value = "std::numeric_limits<I>::min()";
      else
        value = py ? std::to_string(n.literal)
                   : "I{" + std::to_string(n.literal) + "}";
    } else if (n.op == Op::read)
      value = slot(n.slot);
    else if (n.op == Op::empty)
      value = py ? "[]" : "Seq{}";
    else {
      std::vector<std::string> args;
      for (auto &op : n.operands)
        args.push_back(expr(op, level, true));
      if (n.op == Op::call) {
        value = helper(n.slot) + "(fuel";
        for (auto &a : args)
          value += ", " + a;
        value += ")";
      } else if (n.op == Op::less || n.op == Op::equal)
        value = args[0] + (n.op == Op::less ? " < " : " == ") + args[1];
      else if (py && (n.op == Op::add || n.op == Op::sub || n.op == Op::mul)) {
        value = "checked(" + args[0] +
                (n.op == Op::add   ? " + "
                 : n.op == Op::sub ? " - "
                                   : " * ") +
                args[1] + ")";
      } else {
        static const std::map<Op, std::string> names{
            {Op::add, "add"},  {Op::sub, "sub"},       {Op::mul, "mul"},
            {Op::index, "at"}, {Op::append, "append"}, {Op::length, "length"}};
        value = names.at(n.op) + "(" + args[0];
        if (args.size() > 1)
          value += ", " + args[1];
        value += ")";
      }
    }
    // Only nested computations can raise errors or consume shared helper fuel.
    // Bind them in source order before calling a generated C++ function.
    if (!operand || n.op == Op::literal || n.op == Op::read ||
        n.op == Op::empty)
      return value;
    auto t = "t" + std::to_string(temporary++);
    line(level, (py ? "" : "auto ") + t + " = " + value + (py ? "" : ";"));
    return t;
  }
  void block(const std::vector<Node> &nodes, int level) {
    if (nodes.empty() && py)
      line(level, "pass");
    for (auto &n : nodes) {
      auto v = expr(n.operands[0], level);
      if (n.op == Op::assign)
        line(level, slot(n.slot) + " = " + v + (py ? "" : ";"));
      else if (n.op == Op::branch) {
        line(level, py ? "if " + v + ":" : "if (" + v + ") {");
        block(n.body, level + 1);
        line(level, py ? "else:" : "} else {");
        block(n.otherwise, level + 1);
        if (!py)
          line(level, "}");
      } else {
        if (n.op == Op::range)
          v = "indices(" + v + ")";
        else if (!py)
          v = "Seq(" + v + ")";
        line(level, py ? "for " + slot(n.slot) + " in " + v + ":"
                       : "for (I item : " + v + ") {");
        if (!py)
          line(level + 1, slot(n.slot) + " = item;");
        line(level + 1, py ? "tick(fuel)" : "tick(fuel);");
        block(n.body, level + 1);
        if (!py)
          line(level, "}");
      }
    }
  }
  std::string signature(const Function &f, bool wrap, int id) {
    std::string s = py ? "def " : cpp_type(f.result) + " ";
    s += wrap ? "algorithm(" : helper(id) + "(";
    if (!wrap)
      s += py ? "fuel" : "int &fuel";
    for (size_t i = 0; i < f.parameters; ++i) {
      if (!wrap || i)
        s += ", ";
      if (!py)
        s += cpp_type(f.slots[i]) + " ";
      s += slot(i);
    }
    return s + (py ? "):" : ") {");
  }
  std::string generate() {
    const auto &p = program;
    out << (py ? py_runtime : cpp_runtime);
    for (size_t i = 0; i < p.functions.size(); ++i) {
      function_index = i;
      const auto &f = p.functions[i];
      line(0, signature(f, false, i));
      line(1, py ? "tick(fuel)" : "tick(fuel);");
      if (!py)
        for (size_t j = f.parameters; j < f.slots.size(); ++j)
          line(1, cpp_type(f.slots[j]) + " " + slot(j) + "{};");
      block(f.body, 1);
      auto result = expr(f.output, 1);
      line(1, "return " + result + (py ? "" : ";"));
      if (!py)
        line(0, "}");
      out << "\n";
    }
    if (!py)
      line(0, "} // namespace");
    const auto &f = p.functions.back();
    line(0, signature(f, true, 0));
    line(1, py ? "fuel = [100000]" : "int fuel = 100000;");
    std::string call = helper(p.functions.size() - 1) + "(fuel";
    for (size_t i = 0; i < f.parameters; ++i) {
      auto v = slot(i);
      if (f.slots[i] == Type::sequence) {
        if (py)
          line(1, "if type(" + v +
                      ") is not list: raise TypeError('argument type')");
        line(1, "length(" + v + ")" + (py ? "" : ";"));
        if (py)
          line(1, v + " = [checked(x) for x in " + v + "]");
      } else if (py) {
        if (f.slots[i] == Type::integer)
          line(1, "checked(" + v + ")");
        else
          line(1, "if type(" + v +
                      ") is not bool: raise TypeError('argument type')");
      }
      call += ", " + v;
    }
    line(1, "return " + call + ")" + (py ? "" : ";"));
    if (!py)
      line(0, "}");
    return out.str();
  }
};
} // namespace
std::string emit(const Program &p, bool python) {
  return Emitter{p, python, 0, {}}.generate();
}
} // namespace unllm::algorithm
