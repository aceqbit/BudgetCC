#include "harness/matrix.hpp"

#include <algorithm>
#include <chrono>

#include "harness/instrumented.hpp"
#include "link/trace_loader.hpp"

namespace bcc {

MatrixResult runMatrix(const MatrixOptions& opt) {
  std::vector<RunConfig> runs = expandMatrix(opt.phase);
  runs.erase(std::remove_if(runs.begin(), runs.end(),
                            [&](const RunConfig& c) {
                              if (!opt.mode.empty() && c.mode != opt.mode) return true;
                              if (opt.buffer != 0 && c.buffer_pkts != opt.buffer) return true;
                              return !opt.traces.empty() &&
                                     std::find(opt.traces.begin(), opt.traces.end(), c.trace) == opt.traces.end();
                            }),
             runs.end());
  if (opt.limit != 0 && opt.limit < runs.size()) runs.resize(opt.limit);
  std::map<std::string, Trace> traces;
  MatrixResult out;
  const auto t0 = std::chrono::steady_clock::now();
  for (RunConfig& cfg : runs) {
    if (opt.duration != 0) cfg.duration = opt.duration;
    auto it = traces.find(cfg.trace);
    if (it == traces.end()) it = traces.emplace(cfg.trace, Trace::load(resolveTracePath(cfg.trace))).first;
    out.runs.push_back(runInstrumented(cfg, it->second, opt.loki, nullptr));
    if (opt.on_done) opt.on_done(cfg, out.runs.back());
  }
  out.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return out;
}

}  // namespace bcc
