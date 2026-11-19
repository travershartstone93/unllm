#include <opencv2/core.hpp>
#include <opencv2/ml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Words = std::vector<std::string>;
using Labels = std::set<std::string>;
const std::array<std::string, 4> names{"branch", "function", "loop", "print"};

Words split(const std::string &line, char separator) {
  std::istringstream input(line);
  Words fields;
  for (std::string value; std::getline(input, value, separator);)
    fields.push_back(value);
  return fields;
}

Words words(std::string text) {
  for (char &ch : text) {
    auto c = static_cast<unsigned char>(ch);
    ch = std::isalnum(c) ? static_cast<char>(std::tolower(c)) : ' ';
  }
  std::istringstream input(text);
  Words result;
  for (std::string word; input >> word;)
    result.push_back(word);
  return result;
}

struct Example {
  Labels labels;
  std::string text;
  Words tokens;
};

using Splits = std::map<std::string, std::vector<Example>>;

void read_rows(const std::filesystem::path &file, bool lexicon,
               Splits &splits) {
  std::ifstream input(file);
  if (!input)
    throw std::runtime_error("Cannot open " + file.string());
  for (std::string line; std::getline(input, line);) {
    if (line.empty() || line.front() == '#')
      continue;
    auto fields = split(line, '\t');
    if (lexicon)
      fields.insert(fields.begin(), "lexicon");
    if (fields.size() != 3)
      throw std::runtime_error("Invalid fixture row: " + line);
    Labels labels;
    if (fields[1] != "-")
      for (const auto &label : split(fields[1], ',')) {
        if (std::find(names.begin(), names.end(), label) == names.end())
          throw std::runtime_error("Unknown construct: " + label);
        labels.insert(label);
      }
    splits[fields[0]].push_back({labels, fields[2], words(fields[2])});
  }
}

bool explanation(const Words &tokens) {
  static const std::vector<Words> prefixes{{"explain"},
                                           {"describe"},
                                           {"what", "is"},
                                           {"what", "does"},
                                           {"tell", "me", "whether"}};
  for (const auto &prefix : prefixes)
    if (tokens.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), tokens.begin()))
      return true;
  return false;
}

Labels lookup(const Words &tokens, const std::vector<Example> &lexicon) {
  Labels labels;
  for (const auto &phrase : lexicon)
    if (std::search(tokens.begin(), tokens.end(), phrase.tokens.begin(),
                    phrase.tokens.end()) != tokens.end())
      labels.insert(phrase.labels.begin(), phrase.labels.end());
  return labels;
}

Labels features(const Words &tokens) {
  Labels result;
  for (size_t i = 0; i < tokens.size(); ++i) {
    result.insert("word " + tokens[i]);
    if (i)
      result.insert("pair " + tokens[i - 1] + " " + tokens[i]);
    const auto padded = "^" + tokens[i] + "$";
    for (size_t j = 0; j + 3 <= padded.size(); ++j)
      result.insert("tri " + padded.substr(j, 3));
  }
  return result;
}

struct Scored {
  Labels labels;
  double confidence;
};

class Learners {
  std::map<std::string, int> vocabulary;
  cv::Mat training;
  std::vector<Labels> gold;
  std::vector<cv::Ptr<cv::ml::SVM>> classifiers;

public:
  cv::Mat encode(const Words &tokens) const {
    cv::Mat row =
        cv::Mat::zeros(1, static_cast<int>(vocabulary.size()), CV_32F);
    for (const auto &feature : features(tokens)) {
      auto it = vocabulary.find(feature);
      if (it != vocabulary.end())
        row.at<float>(0, it->second) = 1;
    }
    cv::normalize(row, row);
    return row;
  }

  explicit Learners(const std::vector<Example> &fit) {
    for (const auto &row : fit)
      for (const auto &feature : features(row.tokens))
        vocabulary.try_emplace(feature, static_cast<int>(vocabulary.size()));
    for (const auto &row : fit) {
      training.push_back(encode(row.tokens));
      gold.push_back(row.labels);
    }
    for (const auto &name : names) {
      cv::Mat labels(training.rows, 1, CV_32S);
      int positives = 0;
      for (int i = 0; i < labels.rows; ++i) {
        const bool positive = gold[i].contains(name);
        labels.at<int>(i) = positive ? 1 : -1;
        positives += positive;
      }
      auto svm = cv::ml::SVM::create();
      svm->setType(cv::ml::SVM::C_SVC);
      svm->setKernel(cv::ml::SVM::LINEAR);
      svm->setC(1.0);
      cv::Mat weights(2, 1, CV_32F);
      weights.at<float>(0) = labels.rows / (2.0f * (labels.rows - positives));
      weights.at<float>(1) = labels.rows / (2.0f * positives);
      svm->setClassWeights(weights);
      if (!svm->train(training, cv::ml::ROW_SAMPLE, labels))
        throw std::runtime_error("SVM fitting failed");
      classifiers.push_back(svm);
    }
  }

  Scored svm(const cv::Mat &row) const {
    Scored result{{}, std::numeric_limits<double>::infinity()};
    for (size_t i = 0; i < names.size(); ++i) {
      const auto &classifier = classifiers[i];
      if (classifier->predict(row) > 0)
        result.labels.insert(names[i]);
      double raw = classifier->predict(row, cv::noArray(),
                                       cv::ml::StatModel::RAW_OUTPUT);
      result.confidence = std::min(result.confidence, std::abs(raw));
    }
    return result;
  }

  Scored nearest(const cv::Mat &row) const {
    std::vector<double> scores;
    for (int i = 0; i < training.rows; ++i)
      scores.push_back(row.dot(training.row(i)));
    auto it = std::max_element(scores.begin(), scores.end());
    const auto &winner = gold.at(std::distance(scores.begin(), it));
    for (size_t i = 0; i < scores.size(); ++i)
      if (*it - scores[i] <= 1e-6 && gold[i] != winner)
        return {{}, 0};
    return {winner, *it};
  }
};

struct Prediction {
  Labels baseline;
  Scored svm, nearest;
};

Prediction predict(const Example &row, const Learners &learners,
                   const std::vector<Example> &lexicon) {
  if (explanation(row.tokens))
    return {};
  auto vector = learners.encode(row.tokens);
  if (cv::countNonZero(vector) == 0)
    return {};
  return {lookup(row.tokens, lexicon), learners.svm(vector),
          learners.nearest(vector)};
}

Labels accept(const Scored &candidate, int setting) {
  return setting >= 0 && candidate.confidence >= setting / 10.0
             ? candidate.labels
             : Labels{};
}

using Candidate = Scored Prediction::*;

int calibrate(const std::vector<Example> &rows,
              const std::vector<Prediction> &predictions, Candidate method) {
  int best_setting = -1, best_gains = 0;
  for (int setting = 10; setting >= 0; --setting) {
    int gains = 0, errors = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (!predictions[i].baseline.empty())
        continue;
      const auto picked = accept(predictions[i].*method, setting);
      if (!picked.empty()) {
        gains += picked == rows[i].labels;
        errors += picked != rows[i].labels;
      }
    }
    if (errors == 0 && gains > best_gains) {
      best_setting = setting;
      best_gains = gains;
    }
  }
  return best_setting;
}

std::string show(const Labels &labels) {
  std::string result;
  for (const auto &label : labels) {
    if (!result.empty())
      result += ',';
    result += label;
  }
  return result.empty() ? "-" : result;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Usage: trial FIXTURE_DIRECTORY");
    const std::filesystem::path directory(argv[1]);
    Splits splits;
    read_rows(directory / "lexicon.tsv", true, splits);
    read_rows(directory / "requests.tsv", false, splits);
    auto fit = splits.at("train");
    const auto &lexicon = splits.at("lexicon");
    fit.insert(fit.end(), lexicon.begin(), lexicon.end());
    cv::setNumThreads(1);
    cv::setRNGSeed(0);
    const Learners learners(fit);
    std::vector<Prediction> calibration;
    for (const auto &row : splits.at("cal"))
      calibration.push_back(predict(row, learners, lexicon));
    int svm = calibrate(splits.at("cal"), calibration, &Prediction::svm);
    int nearest =
        calibrate(splits.at("cal"), calibration, &Prediction::nearest);
    std::cout << "# Thresholds in tenths (-1 = off): svm " << svm
              << ", nearest " << nearest << '\n'
              << "split\texpected\tlookup\tlookup_svm\tlookup_nearest\tsvm_"
                 "alone\trequest\n";
    for (const auto &split : {"cal", "held", "audit"}) {
      for (const auto &row : splits.at(split)) {
        auto p = predict(row, learners, lexicon);
        auto learned = accept(p.svm, svm);
        std::cout << split << '\t' << show(row.labels) << '\t'
                  << show(p.baseline) << '\t'
                  << show(p.baseline.empty() ? learned : p.baseline) << '\t'
                  << show(p.baseline.empty() ? accept(p.nearest, nearest)
                                             : p.baseline)
                  << '\t' << show(learned) << '\t' << row.text << '\n';
      }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
