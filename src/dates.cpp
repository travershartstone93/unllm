#include <ctime>

#include "unllm/unllm.h"

// Natural-language time expressions, resolved deterministically against an
// injected "now". The policy: a named span ("yesterday", "last week") is
// that whole span; a named moment ("around 3pm", "2 hours ago") becomes a
// 90-minute window centred on it; "since X" runs from X to now.

namespace unllm {

namespace {

struct Civil {
    int year, mon, day, hour = 0, min = 0;
};

int64_t to_epoch(const Civil& c) {
    std::tm tm{};
    tm.tm_year = c.year - 1900;
    tm.tm_mon = c.mon - 1;
    tm.tm_mday = c.day;
    tm.tm_hour = c.hour;
    tm.tm_min = c.min;
    tm.tm_isdst = -1;
    return int64_t(std::mktime(&tm));
}

std::tm local(int64_t t) {
    std::time_t tt = t;
    std::tm tm{};
    localtime_r(&tt, &tm);
    return tm;
}

int64_t midnight(int64_t t, int days_back = 0) {
    std::tm tm = local(t);
    Civil c{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
    return to_epoch(c) - int64_t(days_back) * 86400;
}

const int kDay = 86400;

int weekday_index(const std::string& w) {
    static const char* names[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
    for (int i = 0; i < 7; ++i)
        if (w == names[i]) return i;
    return -1;
}

int month_index(const std::string& w) {
    static const char* names[] = {"january", "february", "march", "april", "may", "june",
                                  "july", "august", "september", "october", "november", "december"};
    for (int i = 0; i < 12; ++i)
        if (w == names[i] || (w.size() >= 3 && std::string(names[i]).substr(0, 3) == w.substr(0, 3) && w.size() <= 4)) return i + 1;
    return -1;
}

// "3pm" -> 15:00, "9" -> 9:00, "21:15" -> 21:15. Returns minutes into the day, or -1.
int clock_minutes(const std::string& w, bool* had_meridiem = nullptr) {
    int h = 0, m = 0;
    std::size_t i = 0;
    while (i < w.size() && isdigit(static_cast<unsigned char>(w[i]))) h = h * 10 + (w[i++] - '0');
    if (i == 0) return -1;
    if (i < w.size() && w[i] == ':') {
        ++i;
        std::size_t j = i;
        while (j < w.size() && isdigit(static_cast<unsigned char>(w[j]))) m = m * 10 + (w[j++] - '0');
        if (j == i) return -1;
        i = j;
    }
    std::string rest = w.substr(i);
    if (had_meridiem) *had_meridiem = !rest.empty();
    if (rest == "pm" && h < 12) h += 12;
    else if (rest == "am" && h == 12) h = 0;
    else if (!rest.empty() && rest != "am" && rest != "pm") return -1;
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

int number_word(const std::string& w) {
    static const std::map<std::string, int> words = {
        {"a", 1}, {"an", 1}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5},
        {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}, {"twenty", 20}, {"thirty", 30}};
    auto it = words.find(w);
    if (it != words.end()) return it->second;
    if (w.empty()) return -1;
    int v = 0;
    for (char c : w) {
        if (!isdigit(static_cast<unsigned char>(c))) return -1;
        v = v * 10 + (c - '0');
    }
    return v;
}

int unit_seconds(const std::string& w) {
    if (w == "minute" || w == "min") return 60;
    if (w == "hour" || w == "hr") return 3600;
    if (w == "day") return kDay;
    if (w == "week") return 7 * kDay;
    if (w == "month") return 30 * kDay;
    return -1;
}

// "10th" / "10" -> 10 when it can be a day of month
int day_number(const std::string& w) {
    std::string d;
    for (char c : w)
        if (isdigit(static_cast<unsigned char>(c))) d += c;
        else break;
    if (d.empty() || d.size() != w.size()) {
        std::string suffix = w.substr(d.size());
        if (suffix != "st" && suffix != "nd" && suffix != "rd" && suffix != "th") return -1;
    }
    if (d.empty()) return -1;
    int v = std::stoi(d);
    return v >= 1 && v <= 31 ? v : -1;
}

}  // namespace

std::optional<TimeWindow> find_time(const std::vector<Token>& tokens, int64_t now,
                                    std::set<std::size_t>* consumed) {
    auto take = [&](std::size_t a, std::size_t b) {
        if (consumed)
            for (std::size_t k = a; k <= b && k < tokens.size(); ++k) consumed->insert(k);
    };
    auto win = [](int64_t a, int64_t b) {
        TimeWindow w;
        w.from = a;
        w.to = b;
        w.stated = true;
        return w;
    };
    auto moment = [&](int64_t t) { return win(t - 45 * 60, t + 45 * 60); };

    const std::tm nowtm = local(now);
    const int64_t today0 = midnight(now);

    // Day-part adjusters applied to a resolved day: "yesterday evening".
    auto day_part = [&](int64_t day0, const std::string& part) -> std::optional<TimeWindow> {
        if (part == "morning") return win(day0 + 5 * 3600, day0 + 12 * 3600);
        if (part == "afternoon") return win(day0 + 12 * 3600, day0 + 18 * 3600);
        if (part == "evening") return win(day0 + 18 * 3600, day0 + 23 * 3600);
        if (part == "night") return win(day0 + 18 * 3600, day0 + 30 * 3600);  // runs into the small hours
        return std::nullopt;
    };

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& w = tokens[i].lower;
        const std::string next = i + 1 < tokens.size() ? tokens[i + 1].lower : "";
        const std::string next2 = i + 2 < tokens.size() ? tokens[i + 2].lower : "";

        // between A and B [yesterday|last night|...]
        if (w == "between") {
            int a = clock_minutes(next), b = clock_minutes(next2 == "and" && i + 3 < tokens.size() ? tokens[i + 3].lower : next2);
            std::size_t bidx = next2 == "and" ? i + 3 : i + 2;
            if (a >= 0 && b >= 0) {
                int64_t day0 = today0;
                std::size_t last = bidx;
                for (std::size_t k = bidx + 1; k < tokens.size() && k <= bidx + 2; ++k) {
                    if (tokens[k].lower == "yesterday" || tokens[k].lower == "night") { day0 = midnight(now, 1); last = k; }
                    if (tokens[k].lower == "last") last = k;
                }
                // evening clocks without am/pm: 9 and 11 at night mean 21 and 23
                if (day0 != today0 && a < 12 * 60 && b < 12 * 60) { a += 12 * 60; b += 12 * 60; }
                take(i, last);
                return win(day0 + int64_t(a) * 60, day0 + int64_t(b) * 60);
            }
        }

        if (w == "yesterday") {
            int64_t d0 = midnight(now, 1);
            if (auto p = day_part(d0, next)) { take(i, i + 1); return p; }
            if (auto cm = clock_minutes(next); cm >= 0) { take(i, i + 1); return moment(d0 + int64_t(cm) * 60); }
            take(i, i);
            return win(d0, d0 + kDay);
        }
        if (w == "today") {
            take(i, i);
            return win(today0, now);
        }
        if (w == "day" && next == "before" && next2 == "yesterday") {
            take(i, i + 2);
            int64_t d0 = midnight(now, 2);
            return win(d0, d0 + kDay);
        }
        if (w == "this" && day_part(today0, next)) {
            take(i, i + 1);
            return day_part(today0, next);
        }
        if (w == "tonight") {
            take(i, i);
            return day_part(today0, "night");
        }

        if (w == "last") {
            if (next == "night") {
                take(i, i + 1);
                return win(midnight(now, 1) + 18 * 3600, today0 + 6 * 3600);
            }
            if (next == "week") {
                take(i, i + 1);
                int since_monday = (nowtm.tm_wday + 6) % 7;
                int64_t this_monday = midnight(now, since_monday);
                return win(this_monday - 7 * kDay, this_monday);
            }
            if (next == "month") {
                take(i, i + 1);
                Civil c{nowtm.tm_year + 1900, nowtm.tm_mon, 1};
                if (c.mon == 0) { c.mon = 12; --c.year; }
                Civil e{nowtm.tm_year + 1900, nowtm.tm_mon + 1, 1};
                return win(to_epoch(c), to_epoch(e));
            }
            if (int wd = weekday_index(next); wd >= 0) {
                take(i, i + 1);
                int back = (nowtm.tm_wday - wd + 7) % 7;
                if (back == 0) back = 7;
                int64_t d0 = midnight(now, back);
                if (auto p = day_part(d0, next2)) { take(i, i + 2); return p; }
                return win(d0, d0 + kDay);
            }
        }

        // since X
        if (w == "since") {
            if (int wd = weekday_index(next); wd >= 0) {
                take(i, i + 1);
                int back = (nowtm.tm_wday - wd + 7) % 7;
                if (back == 0) back = 7;
                return win(midnight(now, back), now);
            }
            if (next == "yesterday") { take(i, i + 1); return win(midnight(now, 1), now); }
            if (next == "the" && (next2 == "update" || next2 == "upgrade")) {
                // the caller resolves "the update" to a real time; we mark the
                // span open and let entities carry the hint
                take(i, i);
                continue;
            }
            if (auto cm = clock_minutes(next); cm >= 0) { take(i, i + 1); return win(today0 + int64_t(cm) * 60, now); }
        }

        // N units ago / past N units
        if ((w == "past" || w == "last") && number_word(next) > 0 && unit_seconds(normalize_word(next2)) > 0) {
            take(i, i + 2);
            return win(now - int64_t(number_word(next)) * unit_seconds(normalize_word(next2)), now);
        }
        if (number_word(w) > 0 && unit_seconds(normalize_word(next)) > 0 && next2 == "ago") {
            take(i, i + 2);
            return moment(now - int64_t(number_word(w)) * unit_seconds(normalize_word(next)));
        }

        // on october 10 / october 10th
        if (int mo = month_index(w); mo >= 1 && w.size() > 2) {
            if (int dn = day_number(next); dn >= 1) {
                take(i, i + 1);
                int year = nowtm.tm_year + 1900;
                Civil c{year, mo, dn};
                int64_t d0 = to_epoch(c);
                if (d0 > now) d0 = to_epoch({year - 1, mo, dn});  // "december 30" asked in january
                return win(d0, d0 + kDay);
            }
        }

        if (w == "this" && next == "week") {
            take(i, i + 1);
            int since_monday = (nowtm.tm_wday + 6) % 7;
            return win(midnight(now, since_monday), now);
        }
        // all month / this month
        if ((w == "all" || w == "this") && next == "month") {
            take(i, i + 1);
            return win(to_epoch({nowtm.tm_year + 1900, nowtm.tm_mon + 1, 1}), now);
        }

        // around 3pm [yesterday] / at 3pm
        if ((w == "around" || w == "at" || w == "about") && clock_minutes(next) >= 0) {
            int cm = clock_minutes(next);
            int64_t day0 = today0;
            std::size_t last = i + 1;
            if (next2 == "yesterday") { day0 = midnight(now, 1); last = i + 2; }
            take(i, last);
            int64_t t = day0 + int64_t(cm) * 60;
            if (t > now && day0 == today0) t -= kDay;  // "at 11pm" said in the morning
            return moment(t);
        }
    }
    return std::nullopt;
}

}  // namespace unllm
