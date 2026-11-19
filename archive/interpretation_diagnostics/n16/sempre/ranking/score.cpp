#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Candidate {
  std::string program;
  double score;
};
struct Case {
  std::string split, request, expected;
  bool covered;
  std::vector<Candidate> pool;
};
struct Decision {
  std::string program;
  double gap;
};

Decision rank(const Case &row, double penalty) {
  double first = -INFINITY, second = -INFINITY;
  std::string winner = "-";
  for (const auto &candidate : row.pool) {
    auto size =
        1 + std::count(candidate.program.begin(), candidate.program.end(), ' ');
    double score = candidate.score - penalty * size;
    if (score > first) {
      second = first;
      first = score;
      winner = candidate.program;
    } else if (score > second) {
      second = score;
    }
  }
  return {winner, row.pool.empty() ? 0.0 : first - second};
}

std::string accept(const Case &row, double penalty, double margin) {
  auto selected = rank(row, penalty);
  if (margin < 0 || !row.covered || selected.gap < margin ||
      selected.program.find("reject") != std::string::npos)
    return "-";
  return selected.program;
}

double calibrate(const std::vector<Case> &rows, double penalty) {
  double threshold = -1;
  int best = 0;
  for (double margin : {4., 2., 1., .5, .25, 0.}) {
    int hits = 0, errors = 0;
    for (const auto &row : rows) {
      if (row.split != "calibration")
        continue;
      auto selected = accept(row, penalty, margin);
      if (selected != "-") {
        hits += selected == row.expected;
        errors += selected != row.expected;
      }
    }
    if (!errors && hits > best) {
      best = hits;
      threshold = margin;
    }
  }
  return threshold;
}

int main() {
  std::vector<Case> rows;
  std::string line;
  std::getline(std::cin, line);
  while (std::getline(std::cin, line)) {
    std::istringstream stream(line);
    std::vector<std::string> fields;
    for (std::string field; std::getline(stream, field, '\t');)
      fields.push_back(field);
    if (fields.size() != 6)
      throw std::runtime_error("Invalid beam record");
    if (rows.empty() || rows.back().request != fields[1] ||
        rows.back().split != fields[0])
      rows.push_back(
          {fields[0], fields[1], fields[2], fields[3] == "true", {}});
    if (fields[4] != "-")
      rows.back().pool.push_back({fields[4], std::stod(fields[5])});
  }
  int best = -1;
  double penalty = 0;
  for (double p : {0., .25, .5, 1., 2., 4., 8.}) {
    int correct = 0;
    for (const auto &row : rows)
      if (row.split == "fit" && row.expected != "-")
        correct += rank(row, p).program == row.expected;
    if (correct > best) {
      best = correct;
      penalty = p;
    }
  }
  double original = calibrate(rows, 0), adjusted = calibrate(rows, penalty);
  std::cout << "# penalty=" << penalty << " fit_hits=" << best
            << " margins=" << original << "," << adjusted << "\n"
            << "expected\traw\tadjusted\toriginal_accepted\tadjusted_"
               "accepted\tpresent\trequest\n";
  for (const auto &row : rows) {
    if (row.split != "transfer")
      continue;
    bool present =
        std::any_of(row.pool.begin(), row.pool.end(),
                    [&](const auto &c) { return c.program == row.expected; });
    std::cout << row.expected << '\t' << rank(row, 0).program << '\t'
              << rank(row, penalty).program << '\t' << accept(row, 0, original)
              << '\t' << accept(row, penalty, adjusted) << '\t' << present
              << '\t' << row.request << '\n';
  }
}
