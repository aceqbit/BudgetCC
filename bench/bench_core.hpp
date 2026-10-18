#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/clock.hpp"

namespace bcc {

struct BenchOptions {
  std::string trace;          // name or path; empty: the first real trace
  std::string algo = "cubic";  // M7 and M9 controller
  Duration duration = 60'000'000'000;
  int repeats = 7;
  std::uint64_t ack_count = 200'000;  // M8 stream length per repeat
};

struct BenchResult {
  double events_per_s = 0.0;               // M7 with the telemetry formatting path on (null sink)
  double events_per_s_no_telemetry = 0.0;  // M7 without telemetry
  double cpu_per_ack_ns = 0.0;             // M8
  double allocs_per_pkt = 0.0;             // M9
};

double median(std::vector<double> v);

// M7: events/s of a full run, median over repeats.
double measureEventsPerSecond(const BenchOptions& o, bool telemetry);
// M8: median ns per Sender::onFeedbackAt + controller call on a scripted feedback stream (BudgetCC).
double measureCpuPerAck(const BenchOptions& o);
// M9: operator-new calls between 5 s and 10 s of simulated time divided by packets sent in that window.
double measureAllocsPerPacket(const BenchOptions& o);

BenchResult runBench(const BenchOptions& o);

// {"schema":1,"phase":..,"git_sha":..,"env":{..},"bench":{events_per_s,..,matrix_runtime_s}}
// sha empty: the sha the binary was configured with.
std::string benchJson(const std::string& phase, const BenchResult& r, double matrix_runtime_s, const std::string& sha = "");
// One bench_history.jsonl line: {"git_sha":..,"change":..,"events_per_s":..,"cpu_per_ack_ns":..,...}
std::string historyLine(const std::string& change, const BenchResult& r, double matrix_runtime_s, const std::string& sha = "");

}  // namespace bcc
