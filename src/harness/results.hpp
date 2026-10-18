#pragma once
#include <string>
#include <vector>

#include "harness/runner.hpp"

namespace bcc {

// One results entry / Loki summary line (CLAUDE.md section 5 field order).
std::string runResultToJson(const RunResult& r);

// {"cpu":..,"os":..,"compiler":..,"version":..,"flags":..,"build_type":..,"power_plan":..}
std::string environmentJson();

struct ResultsHeader {
  std::string phase;
  double matrix_runtime_s = 0.0;
};

// {"schema":1,"phase":..,"git_sha":..,"env":{..},"matrix_runtime_s":..,"runs":[..]}
std::string resultsFileJson(const ResultsHeader& h, const std::vector<std::string>& run_entries);

}  // namespace bcc
