#pragma once
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "harness/runner.hpp"

namespace bcc {

struct MatrixOptions {
  std::string phase = "p1";
  Duration duration = 0;   // 0: keep the 60 s of each run
  std::size_t limit = 0;   // 0: all 250 runs; otherwise only the first N (smoke tests)
  // Called after each finished run (serial, in run order).
  std::function<void(const RunConfig&, const RunResult&)> on_done;
};

struct MatrixResult {
  std::vector<RunResult> runs;
  double runtime_s = 0.0;  // M10: wall-clock seconds for the whole matrix
};

// Runs the experiment matrix. Any failure (including a conservation violation) aborts with an exception.
MatrixResult runMatrix(const MatrixOptions& opt);

}  // namespace bcc
