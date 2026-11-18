#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

double checked_number(double value) {
    if (!std::isfinite(value) || std::abs(value) > 1e15)
        throw std::runtime_error("number outside the finite +/-1e15 domain");
    return value;
}
std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n\v\f");
    if (first == std::string::npos) return "";
    auto last = value.find_last_not_of(" \t\r\n\v\f");
    return value.substr(first, last-first+1);
}
double parse_number(const std::string& raw) {
    const std::string value = trim(raw);
    if (value.empty() || value.find_first_of("xXpP_") != std::string::npos)
        throw std::runtime_error("expected a decimal number");
    char* end = nullptr;
    const double number = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || end != value.c_str()+value.size())
        throw std::runtime_error("expected a decimal number");
    return checked_number(number);
}
std::string checked_line(std::string value, size_t index) {
    if (index > 100000) throw std::runtime_error("input exceeds 100000 rows");
    if (!value.empty() && value.back() == '\r') value.pop_back();
    if (value.size() > 16384) throw std::runtime_error("line too long");
    for (unsigned char ch : value)
        if (ch >= 127 || (ch < 32 && ch != '\t')) throw std::runtime_error("expected printable ASCII and tabs");
    return value;
}
std::string format_number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << checked_number(value);
    return out.str();
}
