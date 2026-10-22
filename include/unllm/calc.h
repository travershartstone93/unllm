#pragma once
// Calculation: arithmetic, percentages, unit conversion and date math,
// exactly. The parser recognizes a calculation or declines; it never guesses,
// so "how many times did firefox crash" and "what is 90 c" are not sums.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace unllm {

struct CalcAnswer {
    std::string text;   // "15% of 80 is 12."
    double value = 0;
    std::string unit;
};

class Calc {
public:
    // data/units.txt: "name;alias;alias | dimension | factor | offset | display"
    bool load_units(const std::string& path);
    // data/holidays.txt: "name;alias | MM-DD" or "name | easter" or "name | nth:N:weekday:month"
    bool load_holidays(const std::string& path);
    std::optional<CalcAnswer> evaluate(const std::string& question, int64_t now) const;

private:
    struct Unit {
        std::string name, dimension, display;
        double factor = 1, offset = 0;
    };
    struct Holiday {
        std::string name, rule;  // "12-25", "easter", "nth:4:4:11"
    };
    std::optional<CalcAnswer> arithmetic(const std::vector<std::string>& w) const;
    std::optional<CalcAnswer> percent(const std::vector<std::string>& w) const;
    std::optional<CalcAnswer> convert(const std::vector<std::string>& w) const;
    std::optional<CalcAnswer> dates(const std::vector<std::string>& w, int64_t now) const;
    const Unit* unit_at(const std::vector<std::string>& w, std::size_t& i) const;
    std::map<std::string, Unit> units_;           // every alias -> its unit
    std::map<std::string, Holiday> holidays_;     // every alias -> its holiday
    std::size_t longest_unit_ = 1, longest_holiday_ = 1;
};

// "12", "4.82803", "37.7778": exact integers, otherwise six significant digits.
std::string format_number(double v);

}  // namespace unllm
