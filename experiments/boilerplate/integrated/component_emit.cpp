// Lower structured operations to a callable provider for the existing C++ emitter.
#include "unllm/json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

using unllm::json::Value;
using Names = std::map<std::string, std::string>;

namespace {
Value load(const char* file) {
    std::ifstream input(file);
    if (!input) throw std::runtime_error(std::string("Cannot read ") + file);
    std::ostringstream contents;
    contents << input.rdbuf();
    return Value::parse(contents.str());
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// Encode bytes directly. Literal text and variable names never become C++ syntax.
std::string literal(const std::string& value) {
    std::ostringstream out;
    out << "std::string(\"" << std::oct << std::setfill('0');
    for (unsigned char byte : value) {
        if (byte == '"' || byte == '\\') out << '\\' << static_cast<char>(byte);
        else if (byte >= 32 && byte < 127) out << static_cast<char>(byte);
        else out << '\\' << std::setw(3) << static_cast<unsigned>(byte);
    }
    out << "\", " << std::dec << value.size() << ")";
    return out.str();
}

struct Lowering {
    const Value& operations;
    const Names& selected;
    const std::string input_type, output_type;
    std::set<std::string> headers, bricks;
    std::ostringstream body;
    unsigned names = 0;
    bool reads = false, writes = false;

    Value operation(const Value& node, bool row) {
        require(node.is_array() && node.size() > 0, "Expected an operation");
        auto name = node.at(0).text();
        auto op = operations[name];
        require(op.is_object(), "Unknown operation: " + name);
        require(node.size() == 1 + static_cast<unsigned>(op["args"].integer()), "Wrong arity: " + name);
        require(row || op["scope"].text() != "row", "Row value outside traversal");
        if (op.contains("brick")) {
            auto id = op["brick"].text(), header = op["header"].text();
            auto match = selected.find(id);
            require(match != selected.end() && match->second == header, "Required brick was not selected: " + id);
            bricks.insert(id);
            headers.insert(header);
        }
        if (op.contains("input")) {
            require(op["input"].text() == input_type, "Incompatible input format: " + name);
            reads = true;
        }
        if (op.contains("output"))
            require(op["output"].text() == output_type, "Incompatible output format: " + name);
        return op;
    }

    std::string expression(const Value& node, const Names& vars, bool row) {
        if (node.is_integer()) {
            if (node.integer() == std::numeric_limits<std::int64_t>::min())
                return "(-9223372036854775807LL - 1)";
            return std::to_string(node.integer()) + "LL";
        }
        if (node.is_string()) return literal(node.text());
        if (node.is_object()) {
            auto it = vars.find(node["ref"].text());
            require(node.size() == 1 && it != vars.end(), "Undeclared variable");
            return it->second;
        }
        auto op = operation(node, row);
        require(!op.contains("kind"), "Statement used as an expression");
        return render(op, node, vars, row);
    }

    std::string render(const Value& op, const Value& node, const Names& vars, bool row) {
        std::vector<std::string> operands;
        for (std::size_t i = 1; i < node.size(); ++i)
            operands.push_back(expression(node.at(i), vars, row));
        const std::string format = op["code"].text();
        std::string code;
        for (std::size_t i = 0; i < format.size(); ++i) {
            if (format[i] != '@') code += format[i];
            else {
                require(++i < format.size() && std::isdigit(static_cast<unsigned char>(format[i])), "Invalid operand template");
                code += operands.at(format[i] - '0');
            }
        }
        return code;
    }

    void block(const Value& lines, Names vars, bool row, unsigned depth) {
        require(lines.is_array() && lines.size() > 0, "Expected a nonempty statement list");
        const std::string indent(4 * depth, ' ');
        for (std::size_t i = 0; i < lines.size(); ++i) {
            auto node = lines.at(i);
            require(node.is_array() && node.size() > 0, "Expected a statement");
            auto name = node.at(0).text();
            if (name == "let") {
                require(node.size() == 3, "Invalid declaration");
                auto key = node.at(1).text();
                require(!key.empty() && !vars.count(key), "Missing or duplicate binding");
                auto value = expression(node.at(2), vars, row);
                auto local = "v" + std::to_string(++names);
                if (key.size() <= 48 && std::all_of(key.begin(), key.end(), [](unsigned char c) {
                        return std::isalnum(c) || c == '_';
                    }))
                    local += "_" + key;
                body << indent << "auto " << local << " = " << value << ";\n";
                vars.emplace(key, local);
            } else if (name == "if") {
                require(node.size() == 3, "Invalid conditional");
                body << indent << "if (" << expression(node.at(1), vars, row) << ") {\n";
                block(node.at(2), vars, row, depth + 1);
                body << indent << "}\n";
            } else {
                auto op = operation(node, row);
                auto kind = op["kind"].text();
                if (kind == "rows") {
                    require(!row, "Nested reads are unsupported");
                    body << indent << op["code"].text() << " {\n";
                    block(node.at(1), vars, true, depth + 1);
                    body << indent << "});\n";
                } else {
                    require(kind == "action" || kind == "sink", "Expected a statement operation");
                    if (kind == "sink") {
                        require(depth == 1 && i + 1 == lines.size(), "Output must finish the outer block");
                        writes = true;
                    }
                    body << indent << render(op, node, vars, row) << ";\n";
                }
            }
        }
    }

    Value provider(const Value& plan, std::size_t slot) {
        block(plan, {}, false, 1);
        require(reads && writes, "Incomplete source-to-destination plan");
        std::ostringstream source;
        source << "#pragma once\n#include <cstdint>\n#include <string>\n#include <utility>\n#include <vector>\n";
        for (const auto& header : headers) source << "#include \"" << header << "\"\n";
        auto fn = "component_" + std::to_string(slot);
        source << "inline void " << fn << "(const char* input_path, const char* output_path) {\n" << body.str() << "}\n";
        Value result = Value::object(), used = Value::array();
        for (const auto& id : bricks) used.push(Value::string(id));
        result.set("slot", Value::number(slot));
        result.set("function", Value::string(fn));
        result.set("source", Value::string(source.str()));
        result.set("bricks", used);
        return result;
    }
};
} // namespace

int main(int argc, char** argv) try {
    require(argc == 6, "Usage: brick-component-emit OPERATIONS SELECTED_HELPERS SPEC ROUTE WIRING");
    auto ops = load(argv[1]), helpers = load(argv[2]), spec = load(argv[3]), route = load(argv[4]), wiring = load(argv[5]);
    auto roles = spec["roles"], rows = route["rows"];
    require(ops.is_object() && helpers.is_array() && roles.is_array() && rows.size() == roles.size(), "Invalid emitter inputs");
    Names selected;
    for (const auto& helper : helpers.items()) selected.emplace(helper["id"].text(), helper["header"].text());
    Value report = Value::object(), providers = Value::array(), gaps = Value::object();
    for (std::size_t i = 0; i < roles.size(); ++i) {
        auto role = roles.at(i);
        auto id = role["id"].text();
        require(rows.at(i)["role"].text() == id, "Route does not match output roles");
        if (rows.at(i)["bound"].flag()) continue;
        try {
            require(wiring.is_object() && !wiring[id].null(), "No structured wiring for this output");
            require(!role["multiple"].flag() && role["content"].text() == "whole", "Unsupported output shape");
            Lowering lower{ops, selected, spec["input_type"].text(), role["output_type"].text(), {}, {}, {}};
            providers.push(lower.provider(wiring[id], i));
        } catch (const std::runtime_error& e) {
            gaps.set(id, Value::string(e.what()));
        }
    }
    report.set("providers", providers);
    report.set("gaps", gaps);
    std::cout << report.dump() << '\n';
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
