#include "formula.h"
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
namespace unllm::maintenance {
namespace {
const std::map<std::string, std::string> GREEK = {
    {"alpha", "α"},   {"beta", "β"},       {"gamma", "γ"}, {"delta", "δ"},
    {"epsilon", "ε"}, {"varepsilon", "ε"}, {"zeta", "ζ"},  {"eta", "η"},
    {"theta", "θ"},   {"vartheta", "ϑ"},   {"iota", "ι"},  {"kappa", "κ"},
    {"lambda", "λ"},  {"mu", "μ"},         {"nu", "ν"},    {"xi", "ξ"},
    {"pi", "π"},      {"varpi", "ϖ"},      {"rho", "ρ"},   {"varrho", "ϱ"},
    {"sigma", "σ"},   {"varsigma", "ς"},   {"tau", "τ"},   {"upsilon", "υ"},
    {"phi", "φ"},     {"varphi", "φ"},     {"chi", "χ"},   {"psi", "ψ"},
    {"omega", "ω"},   {"Gamma", "Γ"},      {"Delta", "Δ"}, {"Theta", "Θ"},
    {"Lambda", "Λ"},  {"Xi", "Ξ"},         {"Pi", "Π"},    {"Sigma", "Σ"},
    {"Upsilon", "Υ"}, {"Phi", "Φ"},        {"Psi", "Ψ"},   {"Omega", "Ω"}};
const std::map<std::string, std::string> SYMBOLS = {{"cdot", "·"},
                                                    {"cdotp", "·"},
                                                    {"times", "×"},
                                                    {"div", "÷"},
                                                    {"pm", "±"},
                                                    {"mp", "∓"},
                                                    {"leq", "≤"},
                                                    {"le", "≤"},
                                                    {"geq", "≥"},
                                                    {"ge", "≥"},
                                                    {"neq", "≠"},
                                                    {"ne", "≠"},
                                                    {"approx", "≈"},
                                                    {"sim", "~"},
                                                    {"simeq", "≃"},
                                                    {"cong", "≅"},
                                                    {"equiv", "≡"},
                                                    {"propto", "∝"},
                                                    {"infty", "∞"},
                                                    {"partial", "∂"},
                                                    {"nabla", "∇"},
                                                    {"to", "→"},
                                                    {"rightarrow", "→"},
                                                    {"Rightarrow", "⇒"},
                                                    {"leftarrow", "←"},
                                                    {"Leftarrow", "⇐"},
                                                    {"leftrightarrow", "↔"},
                                                    {"Leftrightarrow", "⇔"},
                                                    {"mapsto", "↦"},
                                                    {"implies", "⇒"},
                                                    {"iff", "⇔"},
                                                    {"in", "∈"},
                                                    {"notin", "∉"},
                                                    {"ni", "∋"},
                                                    {"subset", "⊂"},
                                                    {"subseteq", "⊆"},
                                                    {"supset", "⊃"},
                                                    {"supseteq", "⊇"},
                                                    {"cup", "∪"},
                                                    {"cap", "∩"},
                                                    {"setminus", "∖"},
                                                    {"emptyset", "∅"},
                                                    {"varnothing", "∅"},
                                                    {"forall", "∀"},
                                                    {"exists", "∃"},
                                                    {"neg", "¬"},
                                                    {"lnot", "¬"},
                                                    {"land", "∧"},
                                                    {"wedge", "∧"},
                                                    {"lor", "∨"},
                                                    {"vee", "∨"},
                                                    {"oplus", "⊕"},
                                                    {"otimes", "⊗"},
                                                    {"sum", "Σ"},
                                                    {"prod", "Π"},
                                                    {"int", "∫"},
                                                    {"iint", "∬"},
                                                    {"oint", "∮"},
                                                    {"circ", "∘"},
                                                    {"bullet", "•"},
                                                    {"ldots", "…"},
                                                    {"cdots", "⋯"},
                                                    {"dots", "…"},
                                                    {"vdots", "⋮"},
                                                    {"hbar", "ħ"},
                                                    {"ell", "ℓ"},
                                                    {"angle", "∠"},
                                                    {"perp", "⊥"},
                                                    {"parallel", "∥"},
                                                    {"prime", "′"},
                                                    {"langle", "⟨"},
                                                    {"rangle", "⟩"},
                                                    {"lceil", "⌈"},
                                                    {"rceil", "⌉"},
                                                    {"lfloor", "⌊"},
                                                    {"rfloor", "⌋"},
                                                    {"mid", "|"},
                                                    {"vert", "|"},
                                                    {"Vert", "‖"},
                                                    {"star", "⋆"},
                                                    {"ast", "*"},
                                                    {"degree", "°"},
                                                    {"deg", "°"},
                                                    {"aleph", "ℵ"},
                                                    {"Re", "ℜ"},
                                                    {"Im", "ℑ"},
                                                    {"uparrow", "↑"},
                                                    {"downarrow", "↓"},
                                                    {"rightleftharpoons", "⇌"},
                                                    {"ll", "≪"},
                                                    {"gg", "≫"},
                                                    {"lt", "<"},
                                                    {"gt", ">"},
                                                    {"colon", ":"},
                                                    {"triangle", "△"},
                                                    {"square", "□"},
                                                    {"checkmark", "✓"}};
const std::map<std::string, std::string> BLACKBOARD = {
    {"R", "ℝ"}, {"N", "ℕ"}, {"Z", "ℤ"}, {"Q", "ℚ"}, {"C", "ℂ"},
    {"P", "ℙ"}, {"E", "𝔼"}, {"F", "𝔽"}, {"H", "ℍ"}};
const std::map<std::string, std::string> SUP = {
    {"0", "⁰"}, {"1", "¹"}, {"2", "²"}, {"3", "³"}, {"4", "⁴"}, {"5", "⁵"},
    {"6", "⁶"}, {"7", "⁷"}, {"8", "⁸"}, {"9", "⁹"}, {"+", "⁺"}, {"-", "⁻"},
    {"=", "⁼"}, {"(", "⁽"}, {")", "⁾"}, {"n", "ⁿ"}, {"i", "ⁱ"}, {"a", "ᵃ"},
    {"x", "ˣ"}, {"y", "ʸ"}, {"t", "ᵗ"}};
const std::map<std::string, std::string> SUB = {
    {"0", "₀"}, {"1", "₁"}, {"2", "₂"}, {"3", "₃"}, {"4", "₄"}, {"5", "₅"},
    {"6", "₆"}, {"7", "₇"}, {"8", "₈"}, {"9", "₉"}, {"+", "₊"}, {"-", "₋"},
    {"=", "₌"}, {"(", "₍"}, {")", "₎"}, {"a", "ₐ"}, {"e", "ₑ"}, {"o", "ₒ"},
    {"x", "ₓ"}, {"n", "ₙ"}, {"i", "ᵢ"}, {"j", "ⱼ"}, {"k", "ₖ"}, {"m", "ₘ"},
    {"p", "ₚ"}, {"s", "ₛ"}, {"t", "ₜ"}, {"r", "ᵣ"}, {"h", "ₕ"}, {"l", "ₗ"},
    {"u", "ᵤ"}};
const std::set<std::string> FUNCTIONS = {
    "Pr",   "arccos", "arcsin", "arctan", "arg",    "bmod",   "cos", "cosh",
    "cot",  "csc",    "det",    "dim",    "exp",    "gcd",    "hom", "inf",
    "ker",  "lcm",    "lg",     "lim",    "liminf", "limsup", "ln",  "log",
    "max",  "min",    "mod",    "pmod",   "rank",   "sec",    "sgn", "sin",
    "sinh", "sup",    "tan",    "tanh",   "tr"};
const std::set<std::string> WORDLIKE = {
    "acute",   "bar",      "bm",           "boldsymbol", "breve",
    "check",   "ddot",     "displaystyle", "dot",        "emph",
    "grave",   "hat",      "mathbf",       "mathcal",    "mathfrak",
    "mathit",  "mathring", "mathrm",       "mathscr",    "mathsf",
    "mathtt",  "mbox",     "operatorname", "overline",   "overrightarrow",
    "text",    "textbf",   "textit",       "textrm",     "textsf",
    "texttt",  "tilde",    "underline",    "unicode",    "vec",
    "widehat", "widetilde"};
const std::set<std::string> DROPPED = {
    "Big",         "Bigg",       "Bigl",
    "Bigr",        "Large",      "big",
    "bigg",        "biggl",      "biggr",
    "bigl",        "bigr",       "boxed",
    "centering",   "color",      "displaystyle",
    "hfill",       "hphantom",   "label",
    "large",       "left",       "limits",
    "middle",      "newcommand", "nolimits",
    "nonumber",    "normalsize", "notag",
    "phantom",     "qquad",      "quad",
    "require",     "right",      "scriptscriptstyle",
    "scriptstyle", "small",      "textstyle",
    "vphantom"};
const std::set<std::string> MATH_SITES = {"astronomy.stackexchange.com",
                                          "biology.stackexchange.com",
                                          "chemistry.stackexchange.com",
                                          "crypto.stackexchange.com",
                                          "cs.stackexchange.com",
                                          "cstheory.stackexchange.com",
                                          "earthscience.stackexchange.com",
                                          "economics.stackexchange.com",
                                          "electronics.stackexchange.com",
                                          "engineering.stackexchange.com",
                                          "hsm.stackexchange.com",
                                          "math.stackexchange.com",
                                          "matheducators.stackexchange.com",
                                          "mathoverflow.net",
                                          "physics.stackexchange.com",
                                          "quant.stackexchange.com",
                                          "quantumcomputing.stackexchange.com",
                                          "stats.stackexchange.com"};
using View = std::string_view;
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
size_t character(View s, size_t pos) {
  ++pos;
  while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xc0) == 0x80)
    ++pos;
  return pos;
}
size_t group_end(View s, size_t pos) {
  int depth = 0;
  for (; pos < s.size(); ++pos) {
    if (s[pos] == '{')
      ++depth;
    if (s[pos] == '}' && --depth == 0)
      return pos;
  }
  return s.size();
}
View argument(View s, size_t &pos) {
  while (pos < s.size() && s[pos] == ' ')
    ++pos;
  if (pos == s.size())
    return {};
  size_t begin = pos;
  if (s[pos] == '{') {
    auto end = group_end(s, pos);
    pos = end + (end < s.size());
    return s.substr(begin + 1, end - begin - 1);
  }
  if (s[pos] == '\\' && ++pos < s.size() && letter(s[pos])) {
    while (pos < s.size() && letter(s[pos]))
      ++pos;
  } else if (pos < s.size())
    pos = character(s, pos);
  return s.substr(begin, pos - begin);
}
std::string wrap(const std::string &s) {
  bool simple = !s.empty();
  for (int32_t i = 0; i < static_cast<int32_t>(s.size());) {
    UChar32 c;
    U8_NEXT(s, i, static_cast<int32_t>(s.size()), c);
    if (!u_isalnum(c) && c != '_' && c != '.' && c != 0xb7 && c != 0x2032)
      simple = false;
  }
  return simple ? s : "(" + s + ")";
}
std::string format(View s, unsigned depth = 0) {
  if (depth >= 64)
    return std::string(s);
  size_t i = 0;
  std::string out;
  auto arg = [&] { return format(argument(s, i), depth + 1); };
  while (i < s.size()) {
    char c = s[i++];
    if (c == '^' || c == '_') {
      auto a = arg(), translated = std::string();
      auto &table = c == '^' ? SUP : SUB;
      for (char digit : a) {
        auto it = table.find(std::string(1, digit));
        if (it == table.end()) {
          translated.clear();
          break;
        }
        translated += it->second;
      }
      out += translated.empty()
                 ? std::string(1, c) + (a.size() == 1 ? a : "(" + a + ")")
                 : translated;
    } else if (c == '\\') {
      if (i == s.size())
        break;
      size_t begin = i;
      if (letter(s[i]))
        while (i < s.size() && letter(s[i]))
          ++i;
      else
        i = character(s, i);
      std::string n(s.substr(begin, i - begin));
      if (n == "frac" || n == "dfrac" || n == "tfrac" || n == "cfrac" ||
          n == "binom") {
        auto a = arg();
        auto b = arg();
        out += n == "binom" ? "C(" + a + ", " + b + ")"
                            : "(" + wrap(a) + "/" + wrap(b) + ")";
      } else if (n == "sqrt") {
        std::string root;
        if (i < s.size() && s[i] == '[') {
          auto end = s.find(']', i + 1);
          if (end != s.npos) {
            root = s.substr(i + 1, end - i - 1);
            i = end + 1;
          }
        }
        out += (root == "3"   ? "∛"
                : root == "4" ? "∜"
                              : (root == "2" ? "" : root) + "√") +
               wrap(arg());
      } else if (n == "ce") {
        auto a = argument(s, i);
        std::string converted;
        bool sub = false;
        for (size_t j = 0; j < a.size();) {
          auto tail = a.substr(j);
          if (tail.starts_with("<=>") || tail.starts_with("->") ||
              tail.starts_with("<-")) {
            converted += tail.starts_with("<=>")  ? "⇌"
                         : tail.starts_with("->") ? "→"
                                                  : "←";
            j += tail.starts_with("<=>") ? 3 : 2;
            sub = false;
            continue;
          }
          char v = a[j++];
          if (sub && v >= '0' && v <= '9')
            converted += SUB.at(std::string(1, v));
          else {
            converted += v;
            sub = letter(v) || v == ')' || v == ']';
          }
        }
        out += format(converted, depth + 1);
      } else if (n == "mathbb") {
        std::string a(argument(s, i));
        out += BLACKBOARD.contains(a) ? BLACKBOARD.at(a) : a;
      } else if (n == "text" || n == "textrm" || n == "mbox" || n == "textit" ||
                 n == "textbf" || n == "emph")
        out += argument(s, i);
      else if (WORDLIKE.contains(n))
        out += arg();
      else if (n == "begin" || n == "end") {
        auto env = argument(s, i);
        if (n == "begin" && (env == "array" || env == "tabular") &&
            i < s.size() && s[i] == '{')
          argument(s, i);
        out += ' ';
      } else if (n == "tag")
        out += " (" + std::string(argument(s, i)) + ")";
      else if (DROPPED.contains(n)) {
        if (n == "label" || n == "color" || n == "phantom" || n == "hphantom" ||
            n == "vphantom" || n == "require")
          argument(s, i);
        if ((n == "left" || n == "right" || n == "middle") && i < s.size() &&
            s[i] == '.')
          ++i;
        if (n == "quad" || n == "qquad")
          out += ' ';
      } else if (GREEK.contains(n))
        out += GREEK.at(n);
      else if (SYMBOLS.contains(n))
        out += SYMBOLS.at(n);
      else if (FUNCTIONS.contains(n))
        out += n + " ";
      else if (n == "\\")
        out += "; ";
      else if (n == "," || n == ";" || n == ":" || n == " " || n == ">")
        out += " ";
      else if (n != "!")
        out += n;
    } else if (c != '{' && c != '}')
      out += (c == '~' || c == '&') ? ' ' : c;
  }
  std::string result;
  bool space = false;
  for (char ch : out) {
    if (std::isspace(static_cast<unsigned char>(ch)))
      space = true;
    else {
      if (space && !result.empty() && result.back() != '(' &&
          View(",.;)").find(ch) == View::npos)
        result += ' ';
      result += ch;
      space = false;
    }
  }
  return result;
}
} // namespace
bool math_site(const std::string &host) { return MATH_SITES.contains(host); }
std::string display_formulas(const std::string &text) {
  constexpr View open = "{\\displaystyle";
  size_t pos = 0;
  std::string out;
  while (pos < text.size()) {
    auto j = text.find(open, pos);
    if (j == text.npos) {
      out += text.substr(pos);
      break;
    }
    out += text.substr(pos, j - pos);
    auto end = group_end(text, j);
    out += format(View(text).substr(j + open.size(), end - j - open.size()));
    pos = end + (end < text.size());
  }
  return out;
}
std::string inline_formulas(const std::string &text) {
  std::string out;
  View s(text);
  for (size_t i = 0; i < s.size();) {
    auto t = s.substr(i);
    if (t.starts_with("\\$")) {
      out += '$';
      i += 2;
      continue;
    }
    View close;
    if (t.starts_with("$$"))
      close = "$$";
    else if (t.starts_with("$"))
      close = "$";
    else if (t.starts_with("\\("))
      close = "\\)";
    else if (t.starts_with("\\["))
      close = "\\]";
    if (close.empty()) {
      out += s[i++];
      continue;
    }
    auto start = i + close.size(), end = s.find(close, start);
    while (close == "$" && end != s.npos && end > 0 && s[end - 1] == '\\')
      end = s.find(close, end + 1);
    if (end == s.npos || end == start ||
        (close == "$" && s.substr(start, end - start).find('\n') != s.npos)) {
      out += s[i++];
      continue;
    }
    out += format(s.substr(start, end - start));
    i = end + close.size();
  }
  return out;
}
} // namespace unllm::maintenance
