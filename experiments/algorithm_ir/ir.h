#pragma once
#include "unllm/symbolic/common.h"
#include <map>
#include <variant>
namespace unllm::algorithm {
using native::Json;
using Sequence = std::vector<int64_t>;
using Value = std::variant<int64_t, bool, Sequence>;
enum class Type { integer, boolean, sequence };
enum class Op {
  literal,
  read,
  empty,
  add,
  sub,
  mul,
  less,
  equal,
  length,
  index,
  append,
  call,
  assign,
  branch,
  each,
  range
};
struct Node {
  Op op;
  Type type;
  int slot = -1;
  int64_t literal = 0;
  std::vector<Node> operands, body, otherwise;
};
struct Function {
  std::string name;
  std::vector<Type> slots;
  std::vector<std::string> names;
  size_t parameters = 0;
  Type result;
  std::vector<Node> body;
  Node output;
};
struct Program {
  std::vector<Function> functions;
};
Program decode(const Json &spec);
Value interpret(const Program &, const std::vector<Value> &);
std::string emit(const Program &, bool python);
Value argument(const Json &, Type);
Json json(const Value &);
} // namespace unllm::algorithm
