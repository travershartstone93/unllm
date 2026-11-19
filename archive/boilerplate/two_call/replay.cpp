#include "unllm/json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using unllm::json::Value;
using Tree = std::map<std::string, std::vector<std::string>>;

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) throw std::runtime_error("Read error " + path.string());
    return buffer.str();
}

Tree load(const fs::path& root) {
    Tree tree;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink()) throw std::runtime_error("Unexpected symlink " + entry.path().string());
        if (entry.is_directory()) continue;
        if (!entry.is_regular_file()) throw std::runtime_error("Expected source file");
        auto& lines = tree[entry.path().lexically_relative(root).generic_string()];
        auto file = read(entry.path());
        if (file.find('\0') != std::string::npos) throw std::runtime_error("Expected text file");
        std::size_t start = 0;
        while (start < file.size()) {
            auto end = file.find('\n', start);
            end = end == std::string::npos ? file.size() : end + 1;
            lines.push_back(file.substr(start, end - start));
            start = end;
        }
    }
    return tree;
}

struct Edit {
    int64_t first, last;
    std::string replacement;
};

void apply_edits(Tree& tree, const Value& patch) {
    if (!patch["edits"].is_array()) throw std::runtime_error("Expected edits array");
    std::map<std::string, std::vector<Edit>> edits;
    for (const auto& item : patch["edits"].items()) {
        const auto path = item["path"].text();
        if (!tree.count(path) || item["first"].type() != json_type_int ||
            item["last"].type() != json_type_int || item["text"].type() != json_type_string)
            throw std::runtime_error("Invalid edit for " + path);
        Edit edit{item["first"].integer(), item["last"].integer(), item["text"].text()};
        if (edit.first < 1 || edit.last < edit.first ||
            edit.last > static_cast<int64_t>(tree.at(path).size()))
            throw std::runtime_error("Invalid original line range for " + path);
        edits[path].push_back(std::move(edit));
    }
    for (auto& [path, changes] : edits) {
        auto& lines = tree.at(path);
        std::sort(changes.begin(), changes.end(), [](const Edit& a, const Edit& b) {
            return a.first > b.first;
        });
        auto boundary = static_cast<int64_t>(lines.size()) + 1;
        for (auto& edit : changes) {
            if (edit.last >= boundary) throw std::runtime_error("Overlapping edits in " + path);
            auto pos = lines.erase(lines.begin() + edit.first - 1, lines.begin() + edit.last);
            if (!edit.replacement.empty()) {
                if (edit.replacement.back() != '\n') edit.replacement += '\n';
                lines.insert(pos, edit.replacement);
            }
            boundary = edit.first;
        }
    }
}

int main(int argc, char** argv) try {
    if (argc != 3 && argc != 5) throw std::runtime_error("Expected show ROOT or apply ROOT PATCH OUT");
    auto tree = load(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "show") {
        for (const auto& [path, lines] : tree) {
            std::cout << "\nFILE " << path << '\n';
            for (std::size_t n = 0; n < lines.size(); ++n) {
                std::cout << n + 1 << '\t' << lines[n];
                if (lines[n].back() != '\n') std::cout << '\n';
            }
        }
    } else if (argc == 5 && std::string(argv[1]) == "apply") {
        apply_edits(tree, Value::parse(read(argv[3])));
        fs::path dest(argv[4]);
        fs::path staging(dest.string() + ".partial");
        if (fs::exists(dest) || !fs::create_directory(staging))
            throw std::runtime_error("Expected fresh output directory");
        try {
            for (const auto& [name, lines] : tree) {
                auto path = staging / name;
                fs::create_directories(path.parent_path());
                std::ofstream out;
                out.exceptions(std::ios::badbit | std::ios::failbit);
                out.open(path, std::ios::binary);
                for (const auto& line : lines) out << line;
                out.close();
            }
            fs::rename(staging, dest);
        } catch (...) {
            fs::remove_all(staging);
            throw;
        }
    } else throw std::runtime_error("Unexpected arguments");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
