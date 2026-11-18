
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
std::string trim(const std::string &value) {
  auto first = value.find_first_not_of(" \t\r\n\v\f");
  if (first == std::string::npos)
    return "";
  auto last = value.find_last_not_of(" \t\r\n\v\f");
  return value.substr(first, last - first + 1);
}
double parse_number(const std::string &raw) {
  const std::string value = trim(raw);
  if (value.empty() || value.find_first_of("xXpP_") != std::string::npos)
    throw std::runtime_error("expected a decimal number");
  char *end = nullptr;
  const double number = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || end != value.c_str() + value.size())
    throw std::runtime_error("expected a decimal number");
  return checked_number(number);
}
std::string checked_line(std::string value, size_t index) {
  if (index > 100000)
    throw std::runtime_error("input exceeds 100000 rows");
  if (!value.empty() && value.back() == '\r')
    value.pop_back();
  if (value.size() > 16384)
    throw std::runtime_error("line too long");
  for (unsigned char ch : value)
    if (ch >= 127 || (ch < 32 && ch != '\t'))
      throw std::runtime_error("expected printable ASCII and tabs");
  return value;
}
std::string format_number(double value) {
  std::ostringstream out;
  out << std::setprecision(17) << checked_number(value);
  return out.str();
}

using Values = std::vector<double>;
using Data = std::map<std::string, Values>;
Data read_input(std::istream &stream) {
  Data data;
  std::string raw;
  size_t index = 0;
  while (std::getline(stream, raw)) {
    ++index;
    if (index > 100000)
      throw std::runtime_error("input exceeds 100000 rows");
    try {
      const auto line = checked_line(raw, index);
      if (trim(line).empty())
        continue;
      const auto split = line.find('\t');
      if (split == std::string::npos ||
          line.find('\t', split + 1) != std::string::npos)
        throw std::runtime_error("expected key TAB number");
      const auto group = trim(line.substr(0, split));
      if (group.empty())
        throw std::runtime_error("empty group key");
      data[group].push_back(parse_number(line.substr(split + 1)));
    } catch (const std::exception &error) {
    }
  }
  if (stream.bad())
    throw std::runtime_error("input read failed");
  return data;
}
Values transform(Values values) {
  {
    Values next;
    for (const auto &x : values)
      if ((x >= (-1.0 * 4.0)))
        next.push_back(x);
    values = std::move(next);
  }
  for (auto &x : values)
    x = checked_number((1.0 + (3.0 * x)));
  {
    std::set<double> seen;
    Values next;
    for (const auto &x : values)
      if (seen.insert(x).second)
        next.push_back(x);
    values = std::move(next);
  }
  std::sort(values.begin(), values.end());
  return values;
}
std::vector<std::string> render_values(const Values &values) {
  if (values.empty())
    throw std::runtime_error("no numeric values remain");
  double total = 0;
  for (double x : values)
    total += x;
  const double minimum = *std::min_element(values.begin(), values.end());
  const double maximum = *std::max_element(values.begin(), values.end());
  const double average = checked_number(total / values.size());
  return {std::to_string(values.size()) + "\t" + format_number(total) + "\t" +
          format_number(minimum) + "\t" + format_number(maximum) + "\t" +
          format_number(average)};
}
std::vector<std::string> process_data(std::istream &stream) {
  auto data = read_input(stream);
  std::vector<std::string> out{"key\tcount\tsum\tmin\tmax\tmean"};
  for (const auto &[group, items] : data) {
    const auto values = transform(items);
    if (values.empty())
      continue;
    for (const auto &row : render_values(values))
      out.push_back(group + "\t" + row);
  }
  return out;
}

int main(int argc, char **argv) {
  try {
    if (argc > 2)
      throw std::runtime_error("usage: program [input-file]");
    std::ifstream file;
    std::istream *stream = &std::cin;
    if (argc == 2) {
      file.open(argv[1]);
      if (!file)
        throw std::runtime_error("cannot open input file");
      stream = &file;
    }
    const auto output = process_data(*stream);
    for (const auto &row : output)
      std::cout << row << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << '\n';
    return 2;
  }
}
