#include "unllm/calc.h"

#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <functional>
#include <set>

namespace unllm {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    std::size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find(sep, i);
        if (j == std::string::npos) j = s.size();
        out.push_back(trim(s.substr(i, j - i)));
        i = j + 1;
    }
    return out;
}

// Words, numbers and operator symbols, lowercased. "(2+3)*4" -> ( 2 + 3 ) * 4;
// "15%" -> 15 %; "100°f" -> 100 °f; "4.5" stays whole.
std::vector<std::string> words_of(const std::string& q) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
    };
    for (std::size_t i = 0; i < q.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(q[i]);
        // the degree sign is two bytes in UTF-8; keep it with what follows
        if (c == 0xC2 && i + 1 < q.size() && static_cast<unsigned char>(q[i + 1]) == 0xB0) {
            flush();
            cur = "°";
            ++i;
            continue;
        }
        if (c == 0xC3 && i + 1 < q.size() && static_cast<unsigned char>(q[i + 1]) == 0x97) {  // ×
            flush();
            out.push_back("*");
            ++i;
            continue;
        }
        if (c == 0xC3 && i + 1 < q.size() && static_cast<unsigned char>(q[i + 1]) == 0xB7) {  // ÷
            flush();
            out.push_back("/");
            ++i;
            continue;
        }
        if (std::isdigit(c) || (c == '.' && !cur.empty() && std::isdigit(static_cast<unsigned char>(cur.back())) &&
                                i + 1 < q.size() && std::isdigit(static_cast<unsigned char>(q[i + 1])))) {
            if (!cur.empty() && !std::isdigit(static_cast<unsigned char>(cur.back())) && cur.back() != '.' &&
                cur != "-")
                flush();
            cur += char(c);
        } else if (std::isalpha(c) || c == '\'' || (c == '/' && !cur.empty() && std::isalpha(static_cast<unsigned char>(cur.back())))) {
            if (!cur.empty() && std::isdigit(static_cast<unsigned char>(cur.back()))) flush();
            cur += char(std::tolower(c));
        } else if (c == ',' && !cur.empty() && std::isdigit(static_cast<unsigned char>(cur.back())) && i + 3 < q.size() &&
                   std::isdigit(static_cast<unsigned char>(q[i + 1])) && std::isdigit(static_cast<unsigned char>(q[i + 2])) &&
                   std::isdigit(static_cast<unsigned char>(q[i + 3]))) {
            continue;  // 1,000
        } else {
            flush();
            if (std::string("+-*/^()%").find(char(c)) != std::string::npos) out.push_back(std::string(1, char(c)));
        }
    }
    flush();
    return out;
}

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    bool digit = false, dot = false;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
        else if (c == '.' && !dot) dot = true;
        else return false;
    }
    return digit;
}

// "two hundred and five", "a dozen", "half", "12" -> a value; i advances.
bool number_at(const std::vector<std::string>& w, std::size_t& i, double& out) {
    if (i >= w.size()) return false;
    if (is_number(w[i])) {
        out = std::stod(w[i]);
        ++i;
        return true;
    }
    static const std::map<std::string, int> small = {
        {"zero", 0}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6}, {"seven", 7},
        {"eight", 8}, {"nine", 9}, {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"thirteen", 13}, {"fourteen", 14},
        {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19}, {"twenty", 20},
        {"thirty", 30}, {"forty", 40}, {"fifty", 50}, {"sixty", 60}, {"seventy", 70}, {"eighty", 80}, {"ninety", 90}};
    static const std::map<std::string, double> scale = {{"hundred", 100}, {"thousand", 1e3}, {"million", 1e6},
                                                        {"billion", 1e9}};
    std::size_t j = i;
    double total = 0, chunk = 0;
    bool any = false;
    if (j < w.size() && w[j] == "half") {
        out = 0.5;
        i = j + 1;
        return true;
    }
    if (j + 1 < w.size() && w[j] == "a" && (w[j + 1] == "dozen" || scale.count(w[j + 1]))) {
        chunk = 1;
        any = true;
        ++j;
    }
    while (j < w.size()) {
        auto s = small.find(w[j]);
        if (s != small.end()) {
            chunk += s->second;
            any = true;
            ++j;
            continue;
        }
        if (w[j] == "dozen" && any) {
            chunk *= 12;
            ++j;
            continue;
        }
        auto sc = scale.find(w[j]);
        if (sc != scale.end() && any) {
            if (sc->second == 100) chunk *= 100;
            else {
                total += chunk * sc->second;
                chunk = 0;
            }
            ++j;
            continue;
        }
        if (w[j] == "and" && any && j + 1 < w.size() && small.count(w[j + 1])) {
            ++j;
            continue;
        }
        break;
    }
    if (!any) return false;
    out = total + chunk;
    i = j;
    return true;
}

// The prefix every calculation may carry: "what is", "calculate", ...
std::size_t skip_lead(const std::vector<std::string>& w) {
    static const std::vector<std::vector<std::string>> leads = {
        {"what", "is"}, {"whats"}, {"what's"}, {"what", "s"}, {"how", "much", "is"}, {"calculate"}, {"compute"},
        {"work", "out"}, {"evaluate"}, {"solve"}, {"tell", "me"}, {"please"}};
    std::size_t i = 0;
    bool again = true;
    while (again) {
        again = false;
        for (const auto& l : leads) {
            if (i + l.size() > w.size()) continue;
            bool m = true;
            for (std::size_t k = 0; k < l.size(); ++k) m &= w[i + k] == l[k];
            if (m) {
                i += l.size();
                again = true;
            }
        }
    }
    return i;
}

std::string capitalize_words(const std::string& s) {
    std::string out = s;
    bool start = true;
    for (char& c : out) {
        if (start && std::isalpha(static_cast<unsigned char>(c))) c = char(std::toupper(static_cast<unsigned char>(c)));
        start = c == ' ';
    }
    return out;
}

int64_t midnight_of(int year, int mon, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;  // noon: safe across daylight-saving changes
    tm.tm_isdst = -1;
    return int64_t(std::mktime(&tm));
}

std::tm local_tm(int64_t t) {
    std::time_t tt = t;
    std::tm tm{};
    localtime_r(&tt, &tm);
    return tm;
}

std::string date_text(int64_t t) {
    std::tm tm = local_tm(t);
    char buf[64];
    std::strftime(buf, sizeof buf, "%A %B %d, %Y", &tm);
    std::string s = buf;
    // "December 05" -> "December 5"
    auto p = s.find(" 0");
    if (p != std::string::npos && p + 2 < s.size() && std::isdigit(static_cast<unsigned char>(s[p + 2]))) s.erase(p + 1, 1);
    return s;
}

int month_of(const std::string& w) {
    static const char* names[] = {"january", "february", "march", "april", "may", "june", "july", "august",
                                  "september", "october", "november", "december"};
    for (int m = 0; m < 12; ++m) {
        std::string n = names[m];
        if (w == n || (w.size() >= 3 && n.compare(0, w.size(), w) == 0 && w != "ma")) return m + 1;
    }
    return 0;
}

}  // namespace

std::string format_number(double v) {
    if (std::isnan(v) || std::isinf(v)) return "undefined";
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", std::round(v));
        return std::string(buf) == "-0" ? "0" : buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

bool Calc::load_units(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto cols = split(line, '|');
        if (cols.size() < 5) continue;
        Unit u;
        auto names = split(cols[0], ';');
        u.name = names[0];
        u.dimension = cols[1];
        u.factor = std::stod(cols[2]);
        u.offset = std::stod(cols[3]);
        u.display = cols[4];
        for (const auto& n : names) {
            if (n.empty()) continue;
            units_[n] = u;
            longest_unit_ = std::max(longest_unit_, words_of(n).size());
        }
    }
    return !units_.empty();
}

bool Calc::load_holidays(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto cols = split(line, '|');
        if (cols.size() < 2) continue;
        auto names = split(cols[0], ';');
        Holiday h{names[0], cols[1]};
        for (const auto& n : names) {
            if (n.empty()) continue;
            std::string key;
            for (const auto& w : words_of(n)) key += (key.empty() ? "" : " ") + w;
            holidays_[key] = h;
            longest_holiday_ = std::max(longest_holiday_, words_of(n).size());
        }
    }
    return !holidays_.empty();
}

const Calc::Unit* Calc::unit_at(const std::vector<std::string>& w, std::size_t& i) const {
    for (std::size_t len = std::min(longest_unit_, w.size() - std::min(i, w.size())); len >= 1; --len) {
        std::string key;
        for (std::size_t k = 0; k < len; ++k) key += (k ? " " : "") + w[i + k];
        auto it = units_.find(key);
        if (it != units_.end()) {
            i += len;
            return &it->second;
        }
        // "°f" may arrive as "°" "f"
        if (len == 2 && w[i] == "°") {
            auto it2 = units_.find("°" + w[i + 1]);
            if (it2 != units_.end()) {
                i += 2;
                return &it2->second;
            }
        }
        if (len == 1) break;
    }
    return nullptr;
}

std::optional<CalcAnswer> Calc::evaluate(const std::string& question, int64_t now) const {
    auto all = words_of(question);
    std::vector<std::string> w(all.begin() + long(skip_lead(all)), all.end());
    while (!w.empty() && (w.back() == "equal" || w.back() == "equals" || w.back() == "please")) w.pop_back();
    if (w.empty()) return std::nullopt;
    if (auto a = dates(all, now)) return a;
    if (auto a = percent(w)) return a;
    if (auto a = convert(all)) return a;
    return arithmetic(w);
}

std::optional<CalcAnswer> Calc::arithmetic(const std::vector<std::string>& w) const {
    // A recursive-descent parser over words and symbols; it must consume the
    // whole question and apply at least one operator.
    std::size_t i = 0;
    int ops = 0;
    bool divzero = false;
    std::string echo;
    auto add_echo = [&](const std::string& s) {
        if (s == ")") echo += ")";
        else if (!echo.empty() && echo.back() == '(') echo += s;
        else echo += (echo.empty() ? "" : " ") + s;
    };
    auto peek = [&](std::initializer_list<const char*> ws) {
        for (const char* x : ws)
            if (i < w.size() && w[i] == x) return true;
        return false;
    };
    auto peek2 = [&](const char* a, const char* b) { return i + 1 < w.size() && w[i] == a && w[i + 1] == b; };
    std::function<bool(double&)> expr, term, factor, unary;
    unary = [&](double& v) -> bool {
        if (peek({"-", "minus", "negative"})) {
            add_echo(w[i] == "-" ? "-" : w[i]);
            ++i;
            if (!unary(v)) return false;
            v = -v;
            return true;
        }
        if (i + 3 < w.size() && w[i] == "the" && w[i + 1] == "square" && w[i + 2] == "root") ++i;
        if (peek({"sqrt"}) || (i + 2 < w.size() && w[i] == "square" && w[i + 1] == "root" && w[i + 2] == "of")) {
            add_echo("the square root of");
            i += w[i] == "sqrt" ? 1 : 3;
            if (!unary(v)) return false;
            v = std::sqrt(v);
            ++ops;
            return true;
        }
        if (peek({"("})) {
            add_echo("(");
            ++i;
            if (!expr(v) || !peek({")"})) return false;
            add_echo(")");
            ++i;
        } else {
            std::size_t start = i;
            if (!number_at(w, i, v)) return false;
            std::string shown;
            for (std::size_t k = start; k < i; ++k) shown += (shown.empty() ? "" : " ") + w[k];
            add_echo(shown);
        }
        while (peek({"squared", "cubed"})) {
            add_echo(w[i]);
            v = w[i] == "squared" ? v * v : v * v * v;
            ++i;
            ++ops;
        }
        return true;
    };
    factor = [&](double& v) -> bool {
        if (!unary(v)) return false;
        if (peek({"^"}) || (i + 3 < w.size() && w[i] == "to" && w[i + 1] == "the" && w[i + 2] == "power" && w[i + 3] == "of")) {
            add_echo(w[i] == "^" ? "^" : "to the power of");
            i += w[i] == "^" ? 1 : 4;
            double e = 0;
            if (!factor(e)) return false;
            v = std::pow(v, e);
            ++ops;
        }
        return true;
    };
    term = [&](double& v) -> bool {
        if (!factor(v)) return false;
        while (true) {
            bool mul = peek({"*", "x", "times"}) || peek2("multiplied", "by");
            bool div = peek({"/", "over"}) || peek2("divided", "by");
            if (!mul && !div) break;
            std::string shown = w[i] == "*" ? "×" : w[i] == "/" ? "÷" : (peek2("multiplied", "by") ? "multiplied by" : peek2("divided", "by") ? "divided by" : w[i]);
            i += (peek2("multiplied", "by") || peek2("divided", "by")) ? 2 : 1;
            add_echo(shown);
            double r = 0;
            if (!factor(r)) return false;
            if (div && r == 0) divzero = true;
            v = mul ? v * r : (r == 0 ? NAN : v / r);
            ++ops;
        }
        return true;
    };
    expr = [&](double& v) -> bool {
        if (!term(v)) return false;
        while (peek({"+", "plus", "-", "minus", "and"})) {
            bool sub = w[i] == "-" || w[i] == "minus";
            if (w[i] == "and") return false;
            add_echo(w[i]);
            ++i;
            double r = 0;
            if (!term(r)) return false;
            v = sub ? v - r : v + r;
            ++ops;
        }
        return true;
    };
    double v = 0;
    if (!expr(v) || i != w.size() || ops == 0) return std::nullopt;
    CalcAnswer a;
    a.value = v;
    a.text = divzero ? "That has no answer: division by zero." : echo + " is " + format_number(v) + ".";
    return a;
}

// "one quarter", "a half", "two thirds", "3/4", "3 over 4" -> a value; i advances.
bool fraction_at(const std::vector<std::string>& w, std::size_t& i, double& out) {
    static const std::map<std::string, double> parts = {
        {"half", 2}, {"halves", 2}, {"third", 3}, {"thirds", 3}, {"quarter", 4}, {"quarters", 4}, {"fourth", 4},
        {"fourths", 4}, {"fifth", 5}, {"fifths", 5}, {"sixth", 6}, {"sixths", 6}, {"eighth", 8}, {"eighths", 8},
        {"tenth", 10}, {"tenths", 10}};
    std::size_t j = i;
    double top = 0;
    if (j < w.size() && w[j] == "a") {
        top = 1;
        ++j;
    } else if (!number_at(w, j, top)) {
        return false;
    }
    if (j < w.size() && parts.count(w[j])) {
        out = top / parts.at(w[j]);
        i = j + 1;
        return true;
    }
    if (j + 1 < w.size() && (w[j] == "/" || w[j] == "over")) {
        std::size_t k = j + 1;
        double bottom = 0;
        if (number_at(w, k, bottom) && bottom != 0) {
            out = top / bottom;
            i = k;
            return true;
        }
    }
    return false;
}

std::optional<CalcAnswer> Calc::percent(const std::vector<std::string>& w) const {
    auto pct = [](const std::string& s) { return s == "%" || s == "percent" || s == "per" || s == "pct"; };
    std::size_t i = 0;
    double x = 0, y = 0;
    // "one quarter as a percentage", "3/4 as a percent"
    {
        std::size_t j = 0;
        double v = 0;
        if (fraction_at(w, j, v) && j + 2 < w.size() + 1 && j < w.size() && w[j] == "as") {
            std::size_t k = j + 1;
            if (k < w.size() && w[k] == "a") ++k;
            if (k + 1 == w.size() && (w[k] == "percent" || w[k] == "percentage")) {
                std::string said;
                for (std::size_t t = 0; t < j; ++t) said += (said.empty() ? "" : " ") + w[t];
                CalcAnswer a;
                a.value = v * 100.0;
                a.text = said + " is " + format_number(a.value) + "%.";
                return a;
            }
        }
    }
    // "15% of 80", "15 percent of 80"
    if (number_at(w, i, x) && i < w.size() && pct(w[i])) {
        std::size_t j = i + 1;
        if (j < w.size() && w[j] == "cent") ++j;
        if (j < w.size() && w[j] == "of") {
            ++j;
            if (number_at(w, j, y) && j == w.size()) {
                CalcAnswer a;
                a.value = x / 100.0 * y;
                a.text = format_number(x) + "% of " + format_number(y) + " is " + format_number(a.value) + ".";
                return a;
            }
        }
    }
    // "what percent is 12 of 80", "what percentage of 80 is 12"
    std::size_t k = 0;
    if (w.size() >= 6 && (w[0] == "what" || w[0] == "which") && (w[1] == "percent" || w[1] == "percentage")) {
        k = 2;
        if (w[k] == "is" || w[k] == "of") {
            bool of_first = w[k] == "of";
            ++k;
            double a1 = 0, b1 = 0;
            if (number_at(w, k, a1) && k < w.size() && (w[k] == "of" || w[k] == "is")) {
                ++k;
                if (number_at(w, k, b1) && k == w.size()) {
                    double part = of_first ? b1 : a1, whole = of_first ? a1 : b1;
                    if (whole == 0) return std::nullopt;
                    CalcAnswer a;
                    a.value = part / whole * 100.0;
                    a.text = format_number(part) + " is " + format_number(a.value) + "% of " + format_number(whole) + ".";
                    return a;
                }
            }
        }
    }
    // "12 is what percent of 80"
    i = 0;
    if (number_at(w, i, x) && i + 3 < w.size() && w[i] == "is" && w[i + 1] == "what" &&
        (w[i + 2] == "percent" || w[i + 2] == "percentage") && w[i + 3] == "of") {
        std::size_t j = i + 4;
        if (number_at(w, j, y) && j == w.size() && y != 0) {
            CalcAnswer a;
            a.value = x / y * 100.0;
            a.text = format_number(x) + " is " + format_number(a.value) + "% of " + format_number(y) + ".";
            return a;
        }
    }
    // "increase 80 by 15%", "decrease 80 by 15 percent"
    if (w.size() >= 4 && (w[0] == "increase" || w[0] == "decrease" || w[0] == "reduce")) {
        std::size_t j = 1;
        if (number_at(w, j, x) && j < w.size() && w[j] == "by") {
            ++j;
            if (number_at(w, j, y) && j < w.size() && pct(w[j]) && j + 1 == w.size()) {
                bool up = w[0] == "increase";
                CalcAnswer a;
                a.value = up ? x * (1 + y / 100.0) : x * (1 - y / 100.0);
                a.text = format_number(x) + (up ? " increased" : " decreased") + " by " + format_number(y) + "% is " +
                         format_number(a.value) + ".";
                return a;
            }
        }
    }
    return std::nullopt;
}

std::optional<CalcAnswer> Calc::convert(const std::vector<std::string>& all) const {
    if (units_.empty()) return std::nullopt;
    std::vector<std::string> w(all.begin() + long(skip_lead(all)), all.end());
    auto finish = [&](double v, const Unit* from, const Unit* to) -> std::optional<CalcAnswer> {
        if (!from || !to || from->dimension != to->dimension || from == to) return std::nullopt;
        double si = (v + from->offset) * from->factor;
        double out = si / to->factor - to->offset;
        // "1 tablespoon", not "1 tablespoons": a plural display word goes
        // singular for exactly one.
        auto shown = [](const Unit* u, double n) {
            const std::string& d = u->display;
            if (n == 1 && d.size() > 2 && d.back() == 's' && std::islower(static_cast<unsigned char>(d[0])))
                return u->name;
            return d;
        };
        CalcAnswer a;
        a.value = out;
        a.unit = to->display;
        a.text = format_number(v) + " " + shown(from, v) + " is " + format_number(out) + " " + shown(to, out) + ".";
        return a;
    };
    // "how many km in 3 miles", "how many km is 3 miles"
    if (w.size() >= 5 && w[0] == "how" && w[1] == "many") {
        std::size_t i = 2;
        const Unit* to = unit_at(w, i);
        if (to && i < w.size()) {
            if (w[i] == "are") ++i;
            if (i < w.size() && (w[i] == "in" || w[i] == "is" || w[i] == "make")) {
                ++i;
                double v = 1;
                std::size_t j = i;
                if (!number_at(w, j, v)) {
                    if (j < w.size() && (w[j] == "a" || w[j] == "an" || w[j] == "one")) ++j;
                    v = 1;
                }
                const Unit* from = unit_at(w, j);
                if (from && j == w.size()) return finish(v, from, to);
            }
        }
    }
    // "convert 100 f to c", "100 f in c", "3 miles to km"
    std::size_t i = 0;
    if (i < w.size() && w[i] == "convert") ++i;
    double v = 0;
    if (!number_at(w, i, v)) return std::nullopt;
    const Unit* from = unit_at(w, i);
    if (!from || i >= w.size() || (w[i] != "to" && w[i] != "in" && w[i] != "into")) return std::nullopt;
    ++i;
    const Unit* to = unit_at(w, i);
    if (!to || i != w.size()) return std::nullopt;
    return finish(v, from, to);
}

std::optional<CalcAnswer> Calc::dates(const std::vector<std::string>& w, int64_t now) const {
    const std::tm today = local_tm(now);
    const int64_t today_noon = midnight_of(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
    // A date at position i: a holiday, today/tomorrow/yesterday, "december 25
    // [2026]", "25 december [2026]". future: roll a past month-day to next year.
    auto date_at = [&](std::size_t& i, bool future, std::string* label) -> int64_t {
        if (i >= w.size()) return 0;
        for (std::size_t len = std::min(longest_holiday_, w.size() - i); len >= 1; --len) {
            std::string key;
            for (std::size_t k = 0; k < len; ++k) key += (k ? " " : "") + w[i + k];
            auto it = holidays_.find(key);
            if (it == holidays_.end()) {
                if (len == 1) break;
                continue;
            }
            const Holiday& h = it->second;
            auto resolve = [&](int year) -> int64_t {
                if (h.rule == "easter") {
                    int a = year % 19, b = year / 100, c = year % 100, d = b / 4, e = b % 4, f = (b + 8) / 25,
                        g = (b - f + 1) / 3, hh = (19 * a + b - d - g + 15) % 30, ii = c / 4, k = c % 4,
                        l = (32 + 2 * e + 2 * ii - hh - k) % 7, m = (a + 11 * hh + 22 * l) / 451;
                    int month = (hh + l - 7 * m + 114) / 31, day = ((hh + l - 7 * m + 114) % 31) + 1;
                    return midnight_of(year, month, day);
                }
                if (h.rule.rfind("nth:", 0) == 0) {
                    auto parts = split(h.rule.substr(4), ':');
                    int nth = std::stoi(parts[0]), wd = std::stoi(parts[1]), mon = std::stoi(parts[2]);
                    if (nth > 0) {
                        std::tm first = local_tm(midnight_of(year, mon, 1));
                        int day = 1 + (wd - first.tm_wday + 7) % 7 + 7 * (nth - 1);
                        return midnight_of(year, mon, day);
                    }
                    int last_day = local_tm(midnight_of(year, mon + 1 > 12 ? 1 : mon + 1, 1) - 86400).tm_mday;
                    if (mon == 12) last_day = 31;
                    std::tm last = local_tm(midnight_of(year, mon, last_day));
                    return midnight_of(year, mon, last_day - (last.tm_wday - wd + 7) % 7);
                }
                int mon = std::stoi(h.rule.substr(0, 2)), day = std::stoi(h.rule.substr(3, 2));
                return midnight_of(year, mon, day);
            };
            int64_t t = resolve(today.tm_year + 1900);
            if (future && t < today_noon) t = resolve(today.tm_year + 1901);
            if (label) *label = capitalize_words(h.name);
            i += len;
            return t;
        }
        if (w[i] == "today") { ++i; if (label) *label = "today"; return today_noon; }
        if (w[i] == "tomorrow") { ++i; if (label) *label = "tomorrow"; return today_noon + 86400; }
        if (w[i] == "yesterday") { ++i; if (label) *label = "yesterday"; return today_noon - 86400; }
        std::size_t j = i;
        int mon = 0, day = 0, year = 0;
        if (int m = month_of(w[j]); m && j + 1 < w.size() && is_number(w[j + 1])) {
            mon = m;
            day = std::stoi(w[j + 1]);
            j += 2;
            if (j < w.size() && (w[j] == "st" || w[j] == "nd" || w[j] == "rd" || w[j] == "th")) ++j;
        } else if (is_number(w[j]) && j + 1 < w.size()) {
            std::size_t k = j + 1;
            if (k < w.size() && (w[k] == "st" || w[k] == "nd" || w[k] == "rd" || w[k] == "th")) ++k;
            if (k < w.size() && (w[k] == "of")) ++k;
            if (k < w.size() && month_of(w[k])) {
                mon = month_of(w[k]);
                day = std::stoi(w[j]);
                j = k + 1;
            }
        }
        if (!mon || day < 1 || day > 31) return 0;
        if (j < w.size() && is_number(w[j]) && w[j].size() == 4) {
            year = std::stoi(w[j]);
            ++j;
        }
        int64_t t = midnight_of(year ? year : today.tm_year + 1900, mon, day);
        if (!year && future && t < today_noon) t = midnight_of(today.tm_year + 1901, mon, day);
        if (label) *label = "";
        i = j;
        return t;
    };
    auto days_between = [](int64_t a, int64_t b) { return int64_t(std::llround(double(b - a) / 86400.0)); };
    // "how many days until christmas", "days till new year", "how long until halloween"
    for (std::size_t k = 0; k + 1 < w.size(); ++k) {
        if ((w[k] == "until" || w[k] == "till" || w[k] == "til" || w[k] == "before") &&
            (k >= 1 && (w[k - 1] == "days" || w[k - 1] == "long" || w[k - 1] == "weeks"))) {
            std::size_t i = k + 1;
            std::string label;
            int64_t t = date_at(i, true, &label);
            if (!t || i != w.size()) continue;
            int64_t d = days_between(today_noon, t);
            CalcAnswer a;
            a.value = double(d);
            if (label.empty()) label = date_text(t);
            if (d == 0) a.text = label + " is today.";
            else if (w[k - 1] == "weeks")
                a.text = format_number(double(d) / 7.0) + " weeks until " + label + " (" + date_text(t) + ").";
            else
                a.text = format_number(double(d)) + (d == 1 ? " day" : " days") + " until " + label +
                         (label == date_text(t) ? "." : " (" + date_text(t) + ").");
            return a;
        }
    }
    // "how many days between march 1 and april 15"
    for (std::size_t k = 0; k + 1 < w.size(); ++k) {
        if (w[k] != "between" || k < 1 || w[k - 1] != "days") continue;
        std::size_t i = k + 1;
        std::string la, lb;
        int64_t a1 = date_at(i, false, &la);
        if (!a1 || i >= w.size() || w[i] != "and") continue;
        ++i;
        int64_t b1 = date_at(i, false, &lb);
        if (!b1 || i != w.size()) continue;
        int64_t d = std::llabs(days_between(a1, b1));
        CalcAnswer a;
        a.value = double(d);
        a.text = "There are " + format_number(double(d)) + " days between " + date_text(std::min(a1, b1)) + " and " +
                 date_text(std::max(a1, b1)) + ".";
        return a;
    }
    // "what day is christmas", "what day of the week was july 4 1776"
    if (w.size() >= 4 && w[0] == "what" && w[1] == "day") {
        std::size_t i = 2;
        if (i + 2 < w.size() && w[i] == "of" && w[i + 1] == "the" && w[i + 2] == "week") i += 3;
        if (i < w.size() && (w[i] == "is" || w[i] == "was")) ++i;
        if (i + 1 < w.size() && w[i] == "will" && w[i + 1] == "be") i += 2;
        std::string label;
        int64_t t = date_at(i, true, &label);
        if (t && i == w.size()) {
            CalcAnswer a;
            std::string full = date_text(t);
            std::string wd = full.substr(0, full.find(' '));
            if (t == today_noon) {
                a.text = "Today is " + full + ".";
                return a;
            }
            a.text = (label.empty() ? full.substr(full.find(' ') + 1) : label + " " + std::to_string(local_tm(t).tm_year + 1900)) +
                     (t < today_noon ? " was a " : t == today_noon ? " is today, a " : " is a ") + wd + ".";
            return a;
        }
    }
    // "what date is 30 days from now", "what is the date in 30 days"
    for (std::size_t k = 0; k + 2 < w.size(); ++k) {
        double n = 0;
        std::size_t i = k;
        if (!number_at(w, i, n) || i >= w.size() || (w[i] != "days" && w[i] != "weeks")) continue;
        bool weeks = w[i] == "weeks";
        ++i;
        bool ahead = (i + 1 < w.size() && w[i] == "from" && (w[i + 1] == "now" || w[i + 1] == "today")) ||
                     (k >= 1 && w[k - 1] == "in" && i == w.size());
        bool back = i < w.size() && w[i] == "ago";
        if (!ahead && !back) continue;
        bool date_q = false;
        for (std::size_t z = 0; z < k; ++z) date_q |= w[z] == "date" || w[z] == "day";
        if (!date_q) continue;
        int64_t t = today_noon + int64_t(std::llround(n * (weeks ? 7 : 1))) * 86400 * (back ? -1 : 1);
        CalcAnswer a;
        a.text = format_number(n) + (weeks ? " weeks" : " days") + (back ? " ago was " : " from now is ") + date_text(t) + ".";
        return a;
    }
    return std::nullopt;
}

}  // namespace unllm
