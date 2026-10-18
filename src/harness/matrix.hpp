#pragma once
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "harness/runner.hpp"
#include "telemetry/loki_pusher.hpp"

namespace bcc {

struct MatrixOptions {
  std::string phase = "p1";
  Duration duration = 0;   // 0: keep the 60 s of each run
  std::size_t limit = 0;   // 0: all 250 runs; otherwise only the first N (smoke tests)
  // Optional filters; run ids keep their numbering from the full matrix.
  std::vector<std::string> traces;
  std::string mode;
  std::uint32_t buffer = 0;
  // Optional telemetry: every run gets its own sink (and pusher thread) built from this config, so the sample
  // ring keeps a single producer however many runs execute at once. base_ns is fixed once for the whole matrix.
  std::optional<LokiConfig> loki;
  std::size_t jobs = 1;  // runs executed concurrently; results are identical for any value
  // Called as each run finishes, one call at a time. For jobs == 1 this is run order.
  std::function<void(const RunConfig&, const RunResult&)> on_done;
};

struct MatrixResult {
  std::vector<RunResult> runs;
  double runtime_s = 0.0;  // M10: wall-clock seconds for the whole matrix
  std::uint64_t push_failures = 0;  // Loki batches given up on, summed over runs
};

// Runs the experiment matrix; result order is run order. Any failure (including a conservation violation)
// aborts with an exception.
MatrixResult runMatrix(const MatrixOptions& opt);

}  // namespace bcc
