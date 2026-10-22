// The calculation exam: exact strings for exact sums, and the questions
// that must NOT be read as sums.

#include <cstdio>
#include <ctime>
#include <string>

#include "unllm/calc.h"

using namespace unllm;

static int checks = 0, failures = 0;

static int64_t T(int year, int mon, int day, int h, int mi) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_isdst = -1;
    return int64_t(std::mktime(&tm));
}

int main() {
    Calc c;
    ++checks;
    if (!c.load_units("data/units.txt") || !c.load_holidays("data/holidays.txt")) {
        ++failures;
        std::printf("  FAIL: tables did not load\n");
    }
    const int64_t NOW = T(2026, 10, 13, 21, 30);
    struct Row { const char* q; const char* want; };  // want "" means: must not parse
    const Row rows[] = {
        {"what is 2 plus 2", "2 plus 2 is 4."},
        {"what is 2 + 3 * 4", "2 + 3 × 4 is 14."},
        {"(2+3)*4", "(2 + 3) × 4 is 20."},
        {"what is 7 times 8", "7 times 8 is 56."},
        {"what's 100 divided by 8", "100 divided by 8 is 12.5."},
        {"calculate 2 to the power of 10", "2 to the power of 10 is 1024."},
        {"what is 12 squared", "12 squared is 144."},
        {"what is the square root of 81", "the square root of 81 is 9."},
        {"square root of 81", "the square root of 81 is 9."},
        {"what is two hundred and five plus five", "two hundred and five plus five is 210."},
        {"what is 15% of 80", "15% of 80 is 12."},
        {"what is 15 percent of 80", "15% of 80 is 12."},
        {"what percent is 12 of 80", "12 is 15% of 80."},
        {"12 is what percent of 80", "12 is 15% of 80."},
        {"increase 80 by 15%", "80 increased by 15% is 92."},
        {"convert 100 f to c", "100 °F is 37.7778 °C."},
        {"how many km in 3 miles", "3 miles is 4.82803 km."},
        {"5 kg in pounds", "5 kg is 11.0231 pounds."},
        {"how many teaspoons in a tablespoon", "1 tablespoon is 3 teaspoons."},
        {"convert 1 mile to km", "1 mile is 1.60934 km."},
        {"how many days until christmas", "73 days until Christmas (Friday December 25, 2026)."},
        {"days till halloween", "18 days until Halloween (Saturday October 31, 2026)."},
        {"how many days until easter", "166 days until Easter (Sunday March 28, 2027)."},
        {"how many days between march 1 and april 15", "There are 45 days between Sunday March 1, 2026 and Wednesday April 15, 2026."},
        {"what day is christmas", "Christmas 2026 is a Friday."},
        {"what is one quarter as a percentage", "one quarter is 25%."},
        {"3/4 as a percent", "3 / 4 is 75%."},
        {"what day is today", "Today is Tuesday October 13, 2026."},
        {"what day of the week was july 4 1776", "July 4, 1776 was a Thursday."},
        {"what is 7 divided by 0", "That has no answer: division by zero."},
        // never sums
        {"how many times did firefox crash this month", ""},
        {"what is 90 c", ""},
        {"what is the capital of france", ""},
        {"how hot is it right now", ""},
        {"2", ""},
    };
    for (const auto& r : rows) {
        ++checks;
        auto a = c.evaluate(r.q, NOW);
        std::string got = a ? a->text : "";
        if (got != r.want) {
            ++failures;
            std::printf("  CALC \"%s\"\n        got  [%s]\n        want [%s]\n", r.q, got.c_str(), r.want);
        }
    }
    std::printf("calc: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
