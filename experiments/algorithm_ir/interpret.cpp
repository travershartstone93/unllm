#include "ir.h"
namespace unllm::algorithm {
using native::Unsupported;
namespace {
struct Machine {
  const Program &program;
  int fuel = 100000;
  void tick() {
    if (--fuel < 0)
      throw Unsupported("work limit");
  }
  Value call(size_t index, std::vector<Value> slots) {
    tick();
    const auto &f = program.functions.at(index);
    if (slots.size() != f.parameters)
      throw Unsupported("argument count");
    for (size_t i = 0; i < slots.size(); ++i) {
      if (slots[i].index() != static_cast<size_t>(f.slots[i]))
        throw Unsupported("argument type");
      if (auto p = std::get_if<Sequence>(&slots[i]); p && p->size() > 64)
        throw Unsupported("sequence bound");
    }
    slots.resize(f.slots.size());
    block(f.body, slots);
    return expr(f.output, slots);
  }
  Value expr(const Node &n, std::vector<Value> &slots) {
    switch (n.op) {
    case Op::literal:
      if (n.type == Type::boolean)
        return bool(n.literal);
      return n.literal;
    case Op::empty:
      return Sequence{};
    case Op::read:
      return slots.at(n.slot);
    default:
      break;
    }
    std::vector<Value> args;
    for (auto &operand : n.operands)
      args.push_back(expr(operand, slots));
    if (n.op == Op::call)
      return call(n.slot, std::move(args));
    if (auto p = std::get_if<Sequence>(&args[0])) {
      if (n.op == Op::length)
        return static_cast<int64_t>(p->size());
      auto v = std::get<int64_t>(args[1]);
      if (n.op == Op::index) {
        if (v < 0 || static_cast<size_t>(v) >= p->size())
          throw Unsupported("index bound");
        return (*p)[v];
      }
      if (p->size() == 64)
        throw Unsupported("sequence bound");
      p->push_back(v);
      return std::move(*p);
    }
    int64_t a = std::get<int64_t>(args[0]), b = std::get<int64_t>(args[1]),
            result;
    bool overflow;
    switch (n.op) {
    case Op::less:
      return a < b;
    case Op::equal:
      return a == b;
    case Op::add:
      overflow = __builtin_add_overflow(a, b, &result);
      break;
    case Op::sub:
      overflow = __builtin_sub_overflow(a, b, &result);
      break;
    case Op::mul:
      overflow = __builtin_mul_overflow(a, b, &result);
      break;
    default:
      throw Unsupported("unexpected expression");
    }
    if (overflow)
      throw Unsupported("integer overflow");
    return result;
  }
  void block(const std::vector<Node> &body, std::vector<Value> &slots) {
    for (auto &n : body) {
      auto value = expr(n.operands[0], slots);
      if (n.op == Op::assign) {
        slots.at(n.slot) = std::move(value);
        continue;
      }
      if (n.op == Op::branch) {
        block(std::get<bool>(value) ? n.body : n.otherwise, slots);
        continue;
      }
      Sequence items;
      if (n.op == Op::each)
        items = std::get<Sequence>(std::move(value));
      else {
        auto count = std::get<int64_t>(value);
        if (count < 0 || count > 64)
          throw Unsupported("range bound");
        for (int64_t i = 0; i < count; ++i)
          items.push_back(i);
      }
      for (auto item : items) {
        tick();
        slots.at(n.slot) = item;
        block(n.body, slots);
      }
    }
  }
};
} // namespace
Value interpret(const Program &p, const std::vector<Value> &args) {
  return Machine{p}.call(p.functions.size() - 1, args);
}
} // namespace unllm::algorithm
