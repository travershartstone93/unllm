#include "signature.h"
#include "unllm/symbolic/common.h"
#include <archive.h>
#include <archive_entry.h>
#include <arrow/api.h>
#include <arrow/io/file.h>
#include <charconv>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <md4c.h>
#include <memory>
#include <optional>
#include <parquet/arrow/reader.h>
#include <set>
#include <string_view>
#include <tree_sitter/api.h>
using namespace unllm::native;
using namespace trial;
namespace fs = std::filesystem;
namespace {
constexpr size_t max_bytes = 128 * 1024;
template <class T> T unpack(arrow::Result<T> r) {
  if (!r.ok())
    throw std::runtime_error(r.status().ToString());
  return std::move(r).ValueUnsafe();
}
std::string_view slice(TSNode n, std::string_view text) {
  return text.substr(ts_node_start_byte(n),
                     ts_node_end_byte(n) - ts_node_start_byte(n));
}
TSNode field(TSNode n, const char *s) {
  return ts_node_child_by_field_name(n, s, std::char_traits<char>::length(s));
}
struct Fragment {
  Signature sig;
  Json ir;
};
Json term(const std::string &name, const std::string &value) {
  auto ir = Json::array();
  ir.push(Json::string(name));
  ir.push(Json::string(value));
  return ir;
}
struct Occurrence {
  unsigned line;
  Json ir;
};
std::optional<Fragment> expression(TSNode n, std::string_view text, int depth,
                                   std::string_view &identifier) {
  if (ts_node_is_null(n))
    return {};
  std::string_view type = ts_node_type(n);
  if (type == "identifier") {
    auto name = slice(n, text);
    if (!identifier.empty() && name != identifier)
      return {};
    identifier = name;
    return Fragment{variable, term("var", "element")};
  }
  if (type == "integer" || type == "number_literal") {
    int v = 0;
    auto s = slice(n, text);
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec == std::errc{} && ptr == s.data() + s.size() && v >= 0 && v <= 12)
      return Fragment{constant(v), term("int", std::to_string(v))};
    return {};
  }
  if (type == "parenthesized_expression" && ts_node_named_child_count(n) == 1)
    return expression(ts_node_named_child(n, 0), text, depth, identifier);
  if (type == "unary_expression" || type == "unary_operator") {
    auto arg = field(n, "argument");
    auto op = field(n, "operator");
    if (ts_node_is_null(arg) || ts_node_is_null(op) || slice(op, text) != "-")
      return {};
    auto kind = std::string_view(ts_node_type(arg));
    if (kind != "integer" && kind != "number_literal")
      return {};
    auto v = expression(arg, text, depth, identifier);
    if (v)
      return Fragment{constant(-v->sig[0]),
                      term("int", std::to_string(-v->sig[0]))};
    return {};
  }
  if (!depth || (type != "binary_expression" && type != "binary_operator"))
    return {};
  auto op = field(n, "operator");
  if (ts_node_is_null(op))
    return {};
  auto s = slice(op, text);
  if (s != "+" && s != "-" && s != "*")
    return {};
  auto a = expression(field(n, "left"), text, depth - 1, identifier);
  if (!a)
    return {};
  auto b = expression(field(n, "right"), text, depth - 1, identifier);
  if (!b)
    return {};
  auto ir = Json::array();
  ir.push(Json::string(s == "+" ? "add" : s == "-" ? "sub" : "mul"));
  ir.push(a->ir);
  ir.push(b->ir);
  return Fragment{combine(s[0], a->sig, b->sig), ir};
}
// Keep operation evidence separate from scalar expression fingerprints.
std::string transition_operation(TSNode n, std::string_view src) {
  std::string_view kind = ts_node_type(n);
  if (kind == "call" || kind == "call_expression") {
    auto fn = field(n, "function"), args = field(n, "arguments");
    if (ts_node_is_null(fn) || ts_node_is_null(args) ||
        ts_node_named_child_count(args) != 2)
      return {};
    auto name = slice(fn, src);
    if (name == "min" || name == "std::min")
      return "min";
    if (name == "max" || name == "std::max")
      return "max";
  }
  if (kind != "augmented_assignment" && kind != "assignment_expression" &&
      kind != "assignment")
    return {};
  auto left = field(n, "left"), right = field(n, "right");
  if (ts_node_is_null(left) || ts_node_is_null(right) ||
      std::string_view(ts_node_type(left)) != "identifier")
    return {};
  auto op = field(n, "operator");
  auto token = ts_node_is_null(op) ? std::string_view{} : slice(op, src);
  if (token == "+=")
    return "add";
  if (token == "-=")
    return "sub";
  if (!token.empty() && token != "=")
    return {};
  auto first = field(right, "left"), binary = field(right, "operator");
  if (ts_node_is_null(first) || ts_node_is_null(binary) ||
      slice(first, src) != slice(left, src))
    return {};
  if (slice(binary, src) == "+")
    return "add";
  if (slice(binary, src) == "-")
    return "sub";
  return {};
}
struct Parser {
  std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser{
      ts_parser_new(), ts_parser_delete};
  explicit Parser(const fs::path &lib, std::string name) {
    auto handle = dlopen(lib.c_str(), RTLD_NOW);
    if (!handle)
      throw std::runtime_error(dlerror());
    auto fn = reinterpret_cast<const TSLanguage *(*)()>(
        dlsym(handle, ("tree_sitter_" + name).c_str()));
    if (!fn || !ts_parser_set_language(parser.get(), fn()))
      throw std::runtime_error("tree-sitter grammar ABI");
  }
  void collect(std::string_view s, std::map<std::string, Occurrence> &found,
               std::map<std::string, unsigned> &operations,
               unsigned offset = 0) {
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser.get(), nullptr, s.data(), s.size()),
        ts_tree_delete);
    if (!tree)
      throw std::runtime_error("tree-sitter returned no tree");
    std::vector<TSNode> pending{ts_tree_root_node(tree.get())};
    while (!pending.empty()) {
      auto n = pending.back();
      pending.pop_back();
      std::string_view kind = ts_node_type(n);
      if (!ts_node_has_error(n)) {
        auto op = transition_operation(n, s);
        if (!op.empty())
          operations.try_emplace(op, offset + 1 + ts_node_start_point(n).row);
      }
      if ((kind == "binary_operator" || kind == "binary_expression") &&
          !ts_node_has_error(n)) {
        std::string_view name;
        auto fragment = expression(n, s, 2, name);
        if (fragment && !name.empty() &&
            fragment->sig != constant(fragment->sig[0]))
          found.try_emplace(key(fragment->sig),
                            Occurrence{offset + 1 + ts_node_start_point(n).row,
                                       fragment->ir});
      }
      for (uint32_t i = ts_node_named_child_count(n); i > 0; --i)
        pending.push_back(ts_node_named_child(n, i - 1));
    }
  }
};
struct Fence {
  std::string lang, code;
};
std::vector<Fence> fences(std::string_view text) {
  struct State {
    std::vector<Fence> blocks;
    bool active = false;
  } state;
  MD_PARSER p{};
  p.enter_block = [](MD_BLOCKTYPE t, void *d, void *u) {
    if (t == MD_BLOCK_CODE) {
      auto &s = *static_cast<State *>(u);
      auto lang = static_cast<MD_BLOCK_CODE_DETAIL *>(d)->lang;
      std::string tag(lang.text ? lang.text : "", lang.size);
      s.active = tag == "cpp" || tag == "c++" || tag == "python";
      if (s.active)
        s.blocks.push_back({tag, {}});
    }
    return 0;
  };
  p.leave_block = [](MD_BLOCKTYPE t, void *, void *u) {
    if (t == MD_BLOCK_CODE)
      static_cast<State *>(u)->active = false;
    return 0;
  };
  p.text = [](MD_TEXTTYPE, const char *s, MD_SIZE len, void *u) {
    auto &st = *static_cast<State *>(u);
    if (st.active)
      st.blocks.back().code.append(s, len);
    return 0;
  };
  p.enter_span = p.leave_span = [](MD_SPANTYPE, void *, void *) { return 0; };
  if (md_parse(text.data(), text.size(), &p, &state))
    throw std::runtime_error("Markdown parse failed");
  return state.blocks;
}
struct Index {
  Parser cpp, python;
  std::set<std::string> selected;
  Json operations = Json::object();
  std::set<std::string> seen;
  std::map<std::string, int> df;
  Json witnesses = Json::object();
  size_t bytes = 0, with_fragments = 0, scanned = 0;
  Index(fs::path libs)
      : cpp(libs / "cpp.so", "cpp"), python(libs / "python.so", "python") {}
  void add(const std::string &id, std::string path, const std::string &src,
           bool markdown) {
    auto ext = fs::path(path).extension();
    bool py = ext == ".py", cc = ext == ".cc" || ext == ".cpp" ||
                                 ext == ".cxx" || ext == ".h" || ext == ".hpp";
    if (!py && !cc && !(markdown && ext == ".md"))
      return;
    auto full = "/" + path + "/";
    for (auto part : {"/tests/", "/test/", "/vendor/", "/third_party/",
                      "/node_modules/", "/.git/"})
      if (full.find(part) != std::string::npos)
        return;
    if (src.empty() || src.size() > max_bytes)
      return;
    auto digest = sha256(src);
    if ((!selected.empty() && !selected.contains(digest)) ||
        !seen.insert(digest).second)
      return;
    bytes += src.size();
    std::map<std::string, Occurrence> found;
    std::map<std::string, unsigned> observed;
    if (cc || py)
      (py ? python : cpp).collect(src, found, observed);
    else
      for (auto &f : fences(src))
        (f.lang == "python" ? python : cpp).collect(f.code, found, observed);
    for (auto &[op, line] : observed) {
      auto evidence = operations[op];
      if (evidence.null()) {
        evidence = Json::object();
        evidence.set("source", Json::string(id));
        evidence.set("path", Json::string(path));
        evidence.set("line", Json::number(line));
      }
      evidence.set("documents",
                   Json::number(evidence["documents"].integer() + 1));
      operations.set(op, evidence);
    }
    if (!found.empty())
      ++with_fragments;
    for (auto &[sig, occurrence] : found) {
      if (++df[sig] == 1) {
        auto w = Json::object();
        w.set("source", Json::string(id));
        w.set("path", Json::string(path));
        w.set("sha256", Json::string(digest));
        if (cc || py)
          w.set("line", Json::number(occurrence.line));
        w.set("ir", occurrence.ir);
        witnesses.set(sig, w);
      }
    }
  }
  void save(const fs::path &out) {
    auto r = Json::object(), counts = Json::object();
    for (auto &[k, v] : df)
      counts.set(k, Json::number(v));
    r.set("df", counts);
    r.set("transition_operations", operations);
    r.set("sources", witnesses);
    r.set("unique_documents", Json::number(seen.size()));
    r.set("contributing_documents", Json::number(with_fragments));
    r.set("eligible_bytes", Json::number(bytes));
    r.set("stack_rows_scanned", Json::number(scanned));
    std::ofstream f(out);
    f.exceptions(std::ios::failbit | std::ios::badbit);
    f << r.dump() << '\n';
  }
};
void curated(Index &idx, const fs::path &root) {
  std::vector<fs::path> dirs;
  for (auto &e : fs::directory_iterator(root))
    if (e.is_directory())
      dirs.push_back(e.path());
  std::sort(dirs.begin(), dirs.end());
  for (auto &dir : dirs) {
    auto provenance = Json::parse(read_file(dir / "provenance.json"));
    auto repo = provenance["repo"].text(),
         id = repo + "@" + provenance["revision"].text();
    if (fs::exists(dir / "files")) {
      for (auto &e : fs::recursive_directory_iterator(dir / "files"))
        if (e.is_regular_file() && e.file_size() <= max_bytes)
          idx.add(id, fs::relative(e.path(), dir / "files"),
                  read_file(e.path()), false);
      continue;
    }
    std::unique_ptr<archive, decltype(&archive_read_free)> ar(
        archive_read_new(), archive_read_free);
    archive_read_support_filter_gzip(ar.get());
    archive_read_support_format_tar(ar.get());
    if (archive_read_open_filename(ar.get(), (dir / "source.tar.gz").c_str(),
                                   65536) != ARCHIVE_OK)
      throw std::runtime_error("open tar");
    archive_entry *entry;
    int rc;
    while ((rc = archive_read_next_header(ar.get(), &entry)) == ARCHIVE_OK) {
      auto n = archive_entry_size(entry);
      if (archive_entry_filetype(entry) != AE_IFREG || n < 1 ||
          n > int64_t(max_bytes))
        continue;
      std::string path = archive_entry_pathname(entry), src(n, '\0');
      path.erase(0, path.find('/') + 1);
      if (archive_read_data(ar.get(), src.data(), n) != n)
        throw std::runtime_error("short tar entry");
      idx.add(id, path, src, repo == "cp-algorithms/cp-algorithms");
    }
    if (rc != ARCHIVE_EOF)
      throw std::runtime_error("tar read failed");
  }
}
void stack(Index &idx, const fs::path &file, int skip, int stop) {
  auto reader = unpack(
      parquet::arrow::OpenFile(unpack(arrow::io::ReadableFile::Open(file)),
                               arrow::default_memory_pool()));
  reader->set_batch_size(32);
  auto batches = unpack(reader->GetRecordBatchReader());
  const std::set<std::string> ok{"MIT",          "Apache-2.0", "BSD-2-Clause",
                                 "BSD-3-Clause", "ISC",        "BSL-1.0",
                                 "0BSD"};
  int pos = 0;
  while (stop < 0 || pos < stop) {
    auto b = unpack(batches->Next());
    if (!b)
      break;
    for (int i = 0; i < b->num_rows() && (stop < 0 || pos < stop); ++i, ++pos) {
      if (pos < skip)
        continue;
      ++idx.scanned;
      auto get = [&](const char *name) {
        auto a = b->GetColumnByName(name);
        if (!a)
          throw std::runtime_error(std::string("missing column ") + name);
        return unpack(a->GetScalar(i))->ToString();
      };
      if (get("is_vendor") != "false" || get("is_generated") != "false" ||
          !ok.contains(get("gha_license_id")))
        continue;
      auto licenses = std::dynamic_pointer_cast<arrow::ListScalar>(
          unpack(b->GetColumnByName("detected_licenses")->GetScalar(i)));
      if (!licenses || !licenses->is_valid)
        continue;
      bool allowed = true;
      for (int j = 0; j < licenses->value->length(); ++j)
        if (!ok.contains(unpack(licenses->value->GetScalar(j))->ToString()))
          allowed = false;
      if (allowed)
        idx.add(get("repo_name") + "@" + get("revision_id"), get("path"),
                get("content"), false);
    }
  }
  if (pos < stop)
    throw std::runtime_error("insufficient shard rows");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc >= 7 && std::string_view(argv[1]) == "--selected") {
      Index idx(argv[2]);
      std::ifstream manifest(argv[3]);
      for (std::string hash; manifest >> hash;)
        idx.selected.insert(hash);
      if (idx.selected.empty() || !manifest.eof())
        throw std::runtime_error("invalid manifest");
      curated(idx, argv[4]);
      for (int i = 6; i < argc; ++i)
        stack(idx, argv[i], 0, -1);
      if (idx.seen != idx.selected)
        throw std::runtime_error("missing selected documents");
      idx.save(argv[5]);
      return 0;
    }
    if (argc < 6)
      throw std::runtime_error("index LIBS CURATED CPP_PARQUET PY_PARQUET "
                               "FRESH_OUTPUT [EXTRA_PARQUET ...]");
    fs::path out = argv[5];
    if (!fs::create_directory(out))
      throw std::runtime_error("output exists");
    auto start = std::chrono::steady_clock::now();
    Index idx(argv[1]);
    curated(idx, argv[2]);
    idx.save(out / "curated.json");
    for (auto [lo, hi] :
         {std::pair{0, 512}, std::pair{512, 8192}, std::pair{8192, -1}}) {
      for (auto file : {argv[3], argv[4]})
        stack(idx, file, lo, hi);
      idx.save(out / (hi < 0 ? "full.json" : std::to_string(hi) + ".json"));
      std::cout << "Cumulative index at " << hi << " rows/language: "
                << std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - start)
                       .count()
                << " seconds" << std::endl;
    }
    if (argc > 6) {
      for (int i = 6; i < argc; ++i)
        stack(idx, argv[i], 0, -1);
      idx.save(out / "expanded.json");
    }
    std::cout << "Index build seconds "
              << std::chrono::duration<double>(
                     std::chrono::steady_clock::now() - start)
                     .count()
              << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
