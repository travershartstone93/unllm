#pragma once
#include "unllm/symbolic/calculator.h"
namespace unllm::native {
struct CalculatorCases {
  Json inputs = Json::array(), values = Json::array(),
       scenarios = Json::array(), buttons = Json::array();
};
CalculatorCases calculator_cases(const CalculatorSpec &);
void check_calculator_results(const CalculatorSpec &, const CalculatorCases &,
                              const Json &);
} // namespace unllm::native
