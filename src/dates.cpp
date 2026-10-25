#include <ctime>

#include "unllm/unllm.h"

// Natural-language time expressions, resolved deterministically against an
// injected "now". The policy: a named span ("yesterday", "last week") is
// that whole span; a named moment ("around 3pm", "2 hours ago") becomes a
// 90-minute window centred on it, a clock to the minute ("around 9:20") a
// 30-minute one, and the window keeps the moment (TimeWindow::at); "since X"
// runs from X to now. A day and a time within it combine in either order:
// "at 3am on tuesday" is "on tuesday at 3am".
//
// A span counted back from now needs no number: "the past hour", "the last
// week" are one of the unit; a vague count reads as a number (a couple 2, a
// few 3, several 5). "Last week" with no "the" is the calendar's. "Lately"
// and "recently" are the last 7 days. The weekend is Saturday 00:00 to
// Monday 00:00 (which one: at its rule).
//
// An hour said without am/pm ("around 1", "between 9 and 10") is read in
// the part of the day named with it: "between 9 and 11 last night" is 21:00
// to 23:00, "9 and 10 yesterday morning" 09:00 to 10:00. With no part named
// it is the waking-day hour: 7 to 11 the morning, 12 noon, 1 to 6 the
// afternoon, so "around 1 yesterday" is 13:00; people name the small hours
// with "am" or "at night". 24-hour clocks ("13:00", "09:20") are as written;
// "noon", "midday" and "midnight" are clocks too.
//
// "Overnight" is a night: alone, the night just past (as "last night"), with
// a day named ("overnight on tuesday"), that day's. The person's own sleep
// ("since i went to bed", "while i was asleep", "after we fell asleep") is in
// no record, so it reads as the night just past too; the laptop's ("since it
// went to sleep") is an event, for the caller to place.
//
// A part of the day in the plural ("what keeps running in the evenings") is a
// habit, not a particular day's. A span named with it holds it ("in the
// mornings last week" is last week); with none it reads as the latest whole
// one (the most recent that has ended), never today's unfinished one, and is
// not marked shifted. One whole evening is what a single window can hold of a
// habit.

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

// "3pm" -> 15:00, "9" -> 9:00, "21:15" -> 21:15, "noon" -> 12:00. Returns minutes into the day, or -1.
int clock_minutes(const std::string& w, bool* had_meridiem = nullptr) {
    // Noon, midday and midnight are clocks that need no am or pm.
    if (w == "noon" || w == "midday" || w == "midnight") {
        if (had_meridiem) *had_meridiem = true;
        return w == "midnight" ? 0 : 12 * 60;
    }
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

// An hour said without am/pm: "1", "9:20" (1 to 12, no leading zero).
bool bare_hour(const std::string& w) {
    bool meridiem = false;
    int cm = clock_minutes(w, &meridiem);
    return !meridiem && cm >= 60 && cm < 13 * 60 && w[0] != '0';
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

// A count said loosely, before a unit: "a couple (of) days" 2, "a few" 3, "several" 5.
int vague_count(const std::string& w) {
    if (w == "couple") return 2;
    if (w == "few") return 3;
    if (w == "several") return 5;
    return -1;
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
    auto moment = [&](int64_t t, int64_t half = 45 * 60) {
        TimeWindow w = win(t - half, t + half);
        w.at = t;
        return w;
    };

    const std::tm nowtm = local(now);
    const int64_t today0 = midnight(now);

    // Day-part adjusters applied to a resolved day: "yesterday evening".
    auto day_part = [&](int64_t day0, const std::string& part) -> std::optional<TimeWindow> {
        if (part == "morning") return win(day0 + 5 * 3600, day0 + 12 * 3600);
        if (part == "afternoon") return win(day0 + 12 * 3600, day0 + 18 * 3600);
        if (part == "evening") return win(day0 + 18 * 3600, day0 + 23 * 3600);
        if (part == "night") return win(day0 + 18 * 3600, day0 + 30 * 3600);  // runs into the small hours
        if (part == "breakfast") return win(day0 + 7 * 3600, day0 + 9 * 3600);
        if (part == "lunch" || part == "lunchtime") return win(day0 + 12 * 3600, day0 + 14 * 3600);
        if (part == "dinner") return win(day0 + 18 * 3600, day0 + 21 * 3600);
        return std::nullopt;
    };

    // "This morning" asked at 02:38 has not begun: the most recent
    // morning is yesterday's, and the window says it moved.
    auto not_future = [&](TimeWindow p) {
        if (p.from >= now) {
            p.from -= kDay;
            p.to -= kDay;
            p.shifted = true;
        }
        return p;
    };
    auto tok = [&](std::size_t k) { return k < tokens.size() ? tokens[k].lower : std::string(); };
    // A clock, not a count: "about 2 hours ago" names no clock.
    auto clock_at = [&](std::size_t k) {
        return clock_minutes(tok(k)) >= 0 && unit_seconds(normalize_word(tok(k + 1))) < 0;
    };
    // A clock in minutes into the day: an hour without am/pm inside the part
    // of the day named with it, else the waking-day hour (see the top). Any
    // clock moves past midnight to stay inside a night ("2am last night").
    // Said of a clock, the morning reaches back to midnight and the evening
    // on to it: "3 in the morning" is 03:00, "11 in the evening" 23:00.
    auto hour_in = [&](const std::string& w, const std::string& part) {
        const int cm = clock_minutes(w);
        const bool bare = bare_hour(w);
        if (auto p = day_part(0, part)) {
            const int64_t a = part == "morning" ? 0 : p->from, b = part == "evening" ? kDay : p->to;
            for (int c : {cm, cm + 12 * 60, cm + 24 * 60})
                if (a <= c * 60 && c * 60 < b && (bare || c != cm + 12 * 60)) return c;
        }
        return bare && cm < 7 * 60 ? cm + 12 * 60 : cm;
    };
    // The day's broad parts begin at 05:00, 12:00, 18:00 and 23:00. "Before
    // lunch" runs from the start of the part lunch begins after (the
    // morning), "after lunch" to the end of the part it ends in (the
    // afternoon). m: minutes after the day's midnight, any sign.
    auto edge = [](int m, bool before) {
        static const int edges[] = {5 * 60, 12 * 60, 18 * 60, 23 * 60};
        const int base = (m >= 0 ? m / 1440 : (m - 1439) / 1440) * 1440;
        const int r = m - base;
        if (before) {
            for (int k = 3; k >= 0; --k)
                if (edges[k] < r) return base + edges[k];
            return base - 1440 + edges[3];
        }
        for (int e : edges)
            if (e > r) return base + e;
        return base + 1440 + edges[0];
    };

    // A day: "yesterday", "today", "tonight", "last night", "(the) day
    // before yesterday", "(on) tuesday", "last tuesday", "(on) october 10".
    // "Last night" and "tonight" carry their part of the day.
    struct Day {
        int64_t day0 = 0;
        std::string part;
        bool so_far = false;     // today, named alone: up to now
        bool this_week = false;  // a bare weekday that is today: a time in it not yet begun is last week's
        bool sleep = false;      // the person's own sleep ("while i was asleep")
        std::size_t last = 0;
    };
    // The person's own sleep from k ("i went to bed", "we were asleep", "i fell asleep", "i slept", "bedtime"):
    // its last word, or npos. The laptop's ("it went to sleep") is not.
    auto person_asleep = [&](std::size_t k) -> std::size_t {
        if (tok(k) == "bedtime") return k;
        if (tok(k) != "i" && tok(k) != "we") return std::string::npos;
        std::size_t j = k + 1;
        if (tok(j) == "was" || tok(j) == "were" || tok(j) == "had" || tok(j) == "fell") ++j;
        if (tok(j) == "asleep" || tok(j) == "sleeping" || tok(j) == "slept") return j;
        if (tok(j) == "in" && tok(j + 1) == "bed") return j + 1;
        if ((tok(j) == "went" || tok(j) == "gone" || tok(j) == "got") && tok(j + 1) == "to" &&
            (tok(j + 2) == "bed" || tok(j + 2) == "sleep"))
            return j + 2;
        return std::string::npos;
    };
    auto day_at = [&](std::size_t k, Day* d) {
        const std::string a = tok(k);
        d->last = k;
        // No record holds when the person slept: "since i went to bed", "while i was asleep" are the night just
        // past, as "last night".
        if (const std::size_t e = person_asleep(k); e != std::string::npos) {
            d->day0 = midnight(now, 1);
            d->part = "night";
            d->sleep = true;
            d->last = e;
            return true;
        }
        if (a == "yesterday") { d->day0 = midnight(now, 1); return true; }
        if (a == "today") { d->day0 = today0; d->so_far = true; return true; }
        if (a == "tonight") { d->day0 = today0; d->part = "night"; return true; }
        if (a == "last" && tok(k + 1) == "night") {
            d->day0 = midnight(now, 1);
            d->part = "night";
            d->last = k + 1;
            return true;
        }
        if (a == "last" && weekday_index(tok(k + 1)) >= 0) {
            int back = (nowtm.tm_wday - weekday_index(tok(k + 1)) + 7) % 7;
            if (back == 0) back = 7;
            d->day0 = midnight(now, back);
            d->last = k + 1;
            return true;
        }
        const std::size_t j = a == "the" || a == "on" ? k + 1 : k;
        if (tok(j) == "day" && tok(j + 1) == "before" && tok(j + 2) == "yesterday") {
            d->day0 = midnight(now, 2);
            d->last = j + 2;
            return true;
        }
        // A bare weekday ("on tuesday", "what happened friday"): the most
        // recent one. Today's weekday means today so far.
        if (int wd = weekday_index(tok(j)); wd >= 0 && a != "the") {
            const std::string prevw = j > 0 ? tokens[j - 1].lower : "";
            if (prevw == "next" || prevw == "since" || prevw == "last" || prevw == "every") return false;
            int back = (nowtm.tm_wday - wd + 7) % 7;
            d->day0 = midnight(now, back);
            d->so_far = back == 0;
            d->this_week = back == 0;
            d->last = j;
            return true;
        }
        // october 10 / october 10th
        if (int mo = month_index(tok(j)); mo >= 1 && tok(j).size() > 2 && a != "the") {
            if (int dn = day_number(tok(j + 1)); dn >= 1) {
                int year = nowtm.tm_year + 1900;
                d->day0 = to_epoch({year, mo, dn});
                if (d->day0 > now) d->day0 = to_epoch({year - 1, mo, dn});  // "december 30" asked in january
                d->last = j + 1;
                return true;
            }
        }
        return false;
    };
    // A time within a day: a part of it ("lunch", "in the morning", "at
    // lunch"), a clock ("at 3am", "around 1", "around 9:20"), "between 9 and
    // 10", or "(right) before / after lunch".
    struct Within {
        enum Kind { Part, Clock, Between, Before, After } kind = Part;
        std::string part;     // Part, Before, After: the part of the day
        std::string a, b;     // Clock: a; Between: a and b, as typed
        bool right = false;   // "right before", "just after": the two hours next to it
        bool habit = false;   // Part said in the plural: "in the evenings"
        bool overnight = false;  // Part "night" said as "overnight": alone, the night just past
        std::size_t last = 0;
    };
    auto within_at = [&](std::size_t k, bool after_day, Within* p) {
        const std::string a = tok(k);
        if (a == "overnight") {
            p->kind = Within::Part;
            p->part = "night";
            p->overnight = true;
            p->last = k;
            return true;
        }
        if (a == "between" && clock_at(k + 1)) {
            const std::size_t bj = tok(k + 2) == "and" ? k + 3 : k + 2;
            if (clock_at(bj)) {
                p->kind = Within::Between;
                p->a = tok(k + 1);
                p->b = tok(bj);
                p->last = bj;
                return true;
            }
        }
        std::size_t j = a == "at" && (tok(k + 1) == "around" || tok(k + 1) == "about") ? k + 1 : k;
        if ((tok(j) == "at" || tok(j) == "around" || tok(j) == "about") && clock_at(j + 1)) {
            p->kind = Within::Clock;
            p->a = tok(j + 1);
            p->last = j + 1;
            return true;
        }
        // "yesterday 3pm", "tuesday 9:20": right after its day a clock needs
        // no "at", but a bare number there is a count, not a clock.
        if (after_day && clock_at(k) && a.find_first_not_of("0123456789") != std::string::npos) {
            p->kind = Within::Clock;
            p->a = a;
            p->last = k;
            return true;
        }
        const bool right = a == "right" || a == "just";
        j = right ? k + 1 : k;
        if (tok(j) == "before" || tok(j) == "after") {
            const std::size_t pj = tok(j + 1) == "the" ? j + 2 : j + 1;
            if (day_part(0, tok(pj))) {
                p->kind = tok(j) == "before" ? Within::Before : Within::After;
                p->part = tok(pj);
                p->right = right;
                p->last = pj;
                return true;
            }
        }
        j = a == "in" || a == "at" || a == "during" || a == "this" ? k + 1 : k;  // "at 3 this morning"
        if (tok(j) == "the") ++j;
        if (day_part(0, tok(j))) {
            p->kind = Within::Part;
            p->part = tok(j);
            p->last = j;
            return true;
        }
        // The plural ("in the evenings", "mornings"): the stemmer's singular is a part of the day.
        if (const std::string one = normalize_word(tok(j)); one != tok(j) && day_part(0, one)) {
            p->kind = Within::Part;
            p->part = one;
            p->habit = true;
            p->last = j;
            return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& w = tokens[i].lower;
        const std::string next = i + 1 < tokens.size() ? tokens[i + 1].lower : "";
        const std::string next2 = i + 2 < tokens.size() ? tokens[i + 2].lower : "";

        // A day and a time within it, next to each other in either order:
        // "at 3am on tuesday", "yesterday at lunch", "around 1 yesterday",
        // "between 9 and 10 yesterday morning". Either may stand alone.
        {
            Day d;
            Within main, part;
            bool has_day = false, has_main = false, has_part = false;
            std::size_t k = i;
            for (int n = 0; n < 3; ++n) {
                Day dd;
                Within pp;
                if (!has_day && day_at(k, &dd)) {
                    d = dd;
                    has_day = true;
                    k = dd.last + 1;
                } else if (within_at(k, has_day, &pp) && !(pp.kind == Within::Part ? has_part : has_main)) {
                    (pp.kind == Within::Part ? part : main) = pp;
                    (pp.kind == Within::Part ? has_part : has_main) = true;
                    k = pp.last + 1;
                } else {
                    break;
                }
            }
            // A lone "at night" names no particular night; a lone "overnight" is the night just past.
            if ((has_day || has_main || has_part) && (has_day || has_main || part.part != "night" || part.overnight)) {
                take(i, k - 1);
                const int64_t day0 = has_day ? d.day0 : has_part && part.overnight ? midnight(now, 1) : today0;
                const std::string ctx = has_part ? part.part : d.part;  // the part of the day named
                // A bare weekday that is today ("friday evening" asked on a
                // Friday afternoon) names the latest such time that has begun:
                // one not yet begun today is last week's. Not marked shifted:
                // that is the plain reading of the name, not a correction.
                auto begun = [&](TimeWindow p) {
                    if (has_day && d.this_week && p.from >= now) {
                        p.from -= 7 * kDay;
                        p.to -= 7 * kDay;
                    }
                    return p;
                };
                if (has_main && main.kind == Within::Clock) {
                    int64_t t = day0 + int64_t(hour_in(main.a, ctx)) * 60;
                    if (!has_day && t > now) t -= kDay;  // "at 11pm" said in the morning
                    if (has_day && d.this_week && t > now) t -= 7 * kDay;
                    if (main.a.find(':') != std::string::npos) return moment(t, 15 * 60);
                    return moment(t);
                }
                if (has_main && main.kind == Within::Between) {
                    int a = hour_in(main.a, ctx), b = hour_in(main.b, ctx);
                    // the end follows the start: "between 5 and 7" is 17:00 to 19:00
                    if (b <= a) b += bare_hour(main.b) && b + 12 * 60 > a ? 12 * 60 : 24 * 60;
                    TimeWindow bw = win(day0 + int64_t(a) * 60, day0 + int64_t(b) * 60);
                    if (!has_day && bw.from > now) {
                        bw.from -= kDay;
                        bw.to -= kDay;
                    }
                    return begun(bw);
                }
                if (has_main) {  // before or after a part of the day
                    const TimeWindow pw = *day_part(day0, main.part);
                    TimeWindow ba;
                    if (main.kind == Within::Before)
                        ba = win(main.right ? pw.from - 2 * 3600 : day0 + int64_t(edge(int((pw.from - day0) / 60), true)) * 60,
                                 pw.from);
                    else
                        ba = win(pw.to, main.right ? pw.to + 2 * 3600 : day0 + int64_t(edge(int((pw.to - day0) / 60), false)) * 60);
                    return has_day ? begun(ba) : not_future(ba);
                }
                // A part of the day in the plural ("keeps running in the evenings")
                // is a habit, not one day's: the span named with it holds it ("in
                // the mornings last week"), else the latest whole one, the one that
                // has ended, with no note that it moved.
                if (has_part && part.habit && !has_day) {
                    std::vector<Token> tail(tokens.begin() + std::ptrdiff_t(std::min(k, tokens.size())), tokens.end());
                    std::set<std::size_t> sub;
                    if (auto span = find_time(tail, now, &sub); span && !span->live) {
                        for (std::size_t s : sub) take(k + s, k + s);
                        return *span;
                    }
                    TimeWindow hw = *day_part(today0, part.part);
                    while (hw.to > now) {
                        hw.from -= kDay;
                        hw.to -= kDay;
                    }
                    return hw;
                }
                // A bare day part ("just the evening"): today's by default; a
                // follow-up fragment may re-anchor it onto the day under discussion.
                if (has_part && part.overnight && !has_day) return *day_part(day0, part.part);
                if (has_part) return has_day ? begun(*day_part(day0, part.part)) : not_future(*day_part(today0, part.part));
                if (!d.part.empty()) {
                    TimeWindow pw = *day_part(day0, d.part);
                    // "last night while i was asleep": the sleep phrase anywhere marks the night as the person's sleep
                    pw.sleep = d.sleep;
                    for (std::size_t k = 0; !pw.sleep && k < tokens.size(); ++k) pw.sleep = person_asleep(k) != std::string::npos;
                    return pw;
                }
                return d.so_far ? win(today0, now) : win(day0, day0 + kDay);
            }
        }

        if (w == "this" && day_part(today0, next)) {
            take(i, i + 1);
            return not_future(*day_part(today0, next));
        }

        // "the other day", "the other night", "the other week": recent, but
        // not the latest. The other day is one of the 2 to 7 days back, the
        // other night (or morning, evening) one of those days' nights, the
        // other week 1 to 3 weeks back.
        if (w == "other" && i > 0 && tokens[i - 1].lower == "the") {
            if (next == "day") {
                take(i - 1, i + 1);
                return win(midnight(now, 7), midnight(now, 1));
            }
            if (next == "week") {
                take(i - 1, i + 1);
                return win(midnight(now, 21), midnight(now, 7));
            }
            if (auto first = day_part(midnight(now, 7), next)) {
                take(i - 1, i + 1);
                return win(first->from, day_part(midnight(now, 2), next)->to);
            }
        }

        // The weekend: Saturday 00:00 to Monday 00:00. "The weekend" ("over
        // the weekend", "on the weekend", "this weekend") is the latest that
        // has begun, up to now while it lasts; "last weekend" and "this past
        // weekend" the latest that has ended; "weekends", a habit, the latest
        // whole one. "Since the weekend" runs from its start (since, below).
        if (normalize_word(w) == "weekend" && tok(i - 1) != "next") {
            const bool past = tok(i - 1) == "last" || tok(i - 1) == "past";
            std::size_t first = past ? i - 1 : i;
            if (tok(first - 1) == "this" || tok(first - 1) == "the") --first;
            int back = (nowtm.tm_wday + 1) % 7;  // days since Saturday
            if ((past || w != "weekend") && back < 2) back += 7;
            take(first, i);
            return win(midnight(now, back), back < 2 ? now : midnight(now, back - 2));
        }

        // "lately", "recently": the last 7 days. "Most recently" orders
        // ("which started most recently"); it names no span.
        if ((w == "lately" || w == "recently") && tok(i - 1) != "most") {
            take(i, i);
            return win(now - 7 * kDay, now);
        }

        // "right now", "now", "currently", "at the moment": not a span of the
        // records but the present, to be measured when the caller can.
        if (w == "now" || w == "currently" || (w == "right" && next == "now") ||
            (w == "at" && next == "the" && next2 == "moment")) {
            std::size_t last = i;
            if (w == "right") last = i + 1;
            if (w == "at") last = i + 2;
            take(i, last);
            TimeWindow lw = win(now - 60, now);
            lw.live = true;
            return lw;
        }

        // "last week", "last month": the calendar's. "The last week" is the
        // 7 days back from now (below).
        if (w == "last" && tok(i - 1) != "the") {
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
            if (clock_at(i + 1)) {
                int64_t t = today0 + int64_t(hour_in(next, "")) * 60;
                if (t > now) t -= kDay;
                take(i, i + 1);
                return win(t, now);
            }
            // since <any other time phrase> ("since this morning", "since last night", "since lunch"):
            // from the start of that window up to now.
            std::vector<Token> tail(tokens.begin() + i + 1, tokens.end());
            std::set<std::size_t> sub;
            if (auto t = find_time(tail, now, &sub); t && !t->live && t->from < now) {
                take(i, i);
                for (std::size_t k : sub) take(i + 1 + k, i + 1 + k);
                return win(t->from, now);
            }
        }

        // N units ago / past N units ("the last 10 minutes before it crashed"
        // counts back from the event, not from now: the caller's to place).
        // A vague count reads as a number ("the past few days": 3); a unit
        // with no number is one of it ("the past hour", "the last week").
        if (w == "past" || w == "last") {
            int n = number_word(next) > 0 ? number_word(next) : vague_count(next);
            std::size_t u = next == "couple" && next2 == "of" ? i + 3 : i + 2;
            if (n <= 0 && normalize_word(next) == next) {  // no count: one, said singular ("last hours" is no span)
                n = 1;
                u = i + 1;
            }
            if (n > 0 && unit_seconds(normalize_word(tok(u))) > 0 && tok(u + 1) != "before" && tok(u + 1) != "after") {
                take(i, u);
                return win(now - int64_t(n) * unit_seconds(normalize_word(tok(u))), now);
            }
        }
        if (number_word(w) > 0 && unit_seconds(normalize_word(next)) > 0 && next2 == "ago") {
            take(i, i + 2);
            return moment(now - int64_t(number_word(w)) * unit_seconds(normalize_word(next)));
        }

        if (w == "this" && next == "week") {
            take(i, i + 1);
            int since_monday = (nowtm.tm_wday + 6) % 7;
            return win(midnight(now, since_monday), now);
        }
        // "all night" means the night just past, same span as "last night"
        if (w == "all" && next == "night") {
            take(i, i + 1);
            return win(midnight(now, 1) + 18 * 3600, today0 + 6 * 3600);
        }
        // all month / this month
        if ((w == "all" || w == "this") && next == "month") {
            take(i, i + 1);
            return win(to_epoch({nowtm.tm_year + 1900, nowtm.tm_mon + 1, 1}), now);
        }
    }
    return std::nullopt;
}

int64_t span_before(const std::vector<Token>& tokens, std::size_t end, std::size_t* first) {
    auto tok = [&](std::size_t k) { return k < tokens.size() ? tokens[k].lower : std::string(); };
    if (end == 0) return 0;
    const std::size_t u = end - 1;
    const int unit = unit_seconds(normalize_word(tok(u)));
    if (unit <= 0) return 0;
    std::size_t f = u;
    int64_t len = 0;
    if (u >= 1 && tok(u - 1) == "half") {  // "the half hour", "a half hour"
        f = u - 1;
        len = unit / 2;
    } else if (u >= 2 && (tok(u - 1) == "an" || tok(u - 1) == "a") && tok(u - 2) == "half") {  // "half an hour"
        f = u - 2;
        len = unit / 2;
    } else if (u >= 1 && number_word(tok(u - 1)) > 0) {  // "10 minutes", "an hour", "the last 5 minutes"
        f = u - 1;
        len = int64_t(number_word(tok(u - 1))) * unit;
        if (f >= 1 && (tok(f - 1) == "last" || tok(f - 1) == "first")) --f;
    } else if (u >= 1 && (tok(u - 1) == "last" || tok(u - 1) == "first")) {  // "the last hour"
        f = u - 1;
        len = unit;
    } else if (u >= 1 && tok(u - 1) == "the") {  // "the hour before"
        f = u - 1;
        len = unit;
    } else {
        return 0;
    }
    if (f >= 1 && (tok(f - 1) == "the" || tok(f - 1) == "a")) --f;
    if (first) *first = f;
    return len;
}

}  // namespace unllm
