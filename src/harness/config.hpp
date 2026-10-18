#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/clock.hpp"

namespace bcc {

struct RunConfig {
  std::vector<std::string> algos;  // one entry per flow
  std::string trace = "step_down";  // name or path
  std::uint32_t buffer_pkts = 1000;
  double budget_ms = 150.0;  // D: BudgetCC input and the budget used for violation_pct
  std::string mode = "bulk";  // bulk | video | fairness
  Duration prop_delay = 20'000'000;
  Duration duration = 60'000'000'000;
  std::uint64_t seed = 1;
  std::string phase = "p1";
  std::map<std::string, double> cc_params;
  std::uint32_t run_index = 0;

  std::string loki_url;
  std::string csv_dir;

  std::string runId() const;       // "r0042"
  std::string algoLabel() const;   // "cubic" or "budgetcc+cubic"
  bool usesBudget() const;         // any flow is budgetcc
  std::string budgetLabel() const; // "none" or the budget in ms
  std::uint64_t bufferBytes() const { return static_cast<std::uint64_t>(buffer_pkts) * 1250U; }
  // Queue-delay target used for M5 for every algorithm: (150 - propagation) x 0.8.
  double recoveryQstarMs() const;
};

// Turns mode=bulk with several flows into mode=fairness. Throws std::invalid_argument on bad input.
void normalize(RunConfig& c);
void validate(const RunConfig& c);

const std::vector<std::string>& realTraceNames();
const std::vector<std::string>& syntheticTraceNames();
std::vector<std::string> allTraceNames();  // 6 real + 4 synthetic

// 250 runs: 80 baselines + 60 BudgetCC budget sweep + 50 video + 60 fairness (CLAUDE.md decisions 6-7).
std::vector<RunConfig> expandMatrix(const std::string& phase);

// Finds a trace by path, or by name under <traces>/synthetic and <traces>/real.
std::string resolveTracePath(const std::string& name_or_path);

}  // namespace bcc
