#pragma once
// Run-level metrics M1-M6 (definitions: CLAUDE.md section 4, decision 5).
#include <cstdint>
#include <optional>
#include <vector>

#include "link/trace_loader.hpp"

namespace bcc {

struct SummaryInput {
  std::vector<const std::vector<std::int64_t>*> owd_ns;  // per flow, one-way delay of each delivered packet
  std::vector<std::uint64_t> flow_bytes;                  // wire bytes received per flow
  const Trace* trace = nullptr;
  Duration duration = 0;
  double budget_ms = 150.0;
  double qstar_ms = 104.0;
  const std::vector<double>* q_ms = nullptr;  // ground-truth queue delay, one value per 10 ms tick (t = 10, 20, ... ms)
};

struct Recovery {
  std::optional<double> median_ms;  // nullopt when the run has no capacity-drop event
  std::uint32_t events = 0;
  std::uint32_t censored = 0;
};

struct Summary {
  double utilisation_pct = 0.0;
  double p95_owd_ms = 0.0;
  double p99_owd_ms = 0.0;
  double violation_pct = 0.0;
  double violation_pct_d100 = 0.0;
  double violation_pct_d150 = 0.0;
  double violation_pct_d300 = 0.0;
  Recovery recovery;
  std::optional<double> jain;  // two or more flows only
  std::uint64_t delivered_bytes = 0;
};

// Nearest rank: the ceil(pct/100 * N)-th smallest value. `sorted` must be ascending and non-empty.
std::int64_t percentileNearestRank(const std::vector<std::int64_t>& sorted, int pct);
double jainIndex(const std::vector<double>& x);
Recovery recoveryStats(const Trace& trace, const std::vector<double>& q_ms, Duration duration, double qstar_ms);
Summary computeSummary(const SummaryInput& in);

}  // namespace bcc
