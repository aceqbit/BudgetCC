#include "harness/matrix.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

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
  if (opt.duration != 0)
    for (RunConfig& cfg : runs) cfg.duration = opt.duration;

  // Traces are immutable once loaded, so concurrent runs share them.
  std::map<std::string, Trace> traces;
  for (const RunConfig& cfg : runs)
    if (traces.find(cfg.trace) == traces.end()) traces.emplace(cfg.trace, Trace::load(resolveTracePath(cfg.trace)));

  LokiConfig loki_cfg;
  if (opt.loki) {
    loki_cfg = *opt.loki;
    if (loki_cfg.base_ns == 0) loki_cfg.base_ns = defaultBaseNs();
  }

  MatrixResult out;
  out.runs.resize(runs.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::uint64_t> push_failures{0};
  std::atomic<bool> failed{false};
  std::exception_ptr error;
  std::mutex m;  // guards error and on_done
  auto worker = [&] {
    for (;;) {
      const std::size_t i = next.fetch_add(1);
      if (i >= runs.size() || failed.load()) return;
      try {
        std::unique_ptr<TelemetrySink> sink;
        if (opt.loki) sink = std::make_unique<TelemetrySink>(loki_cfg);
        out.runs[i] = runInstrumented(runs[i], traces.at(runs[i].trace), sink.get(), nullptr);
        if (sink) push_failures += sink->counters().push_failures;
        std::lock_guard<std::mutex> lk(m);
        if (opt.on_done) opt.on_done(runs[i], out.runs[i]);
      } catch (...) {
        std::lock_guard<std::mutex> lk(m);
        if (!error) error = std::current_exception();
        failed = true;
        return;
      }
    }
  };

  const auto t0 = std::chrono::steady_clock::now();
  const std::size_t jobs = std::max<std::size_t>(1, std::min(opt.jobs, runs.size()));
  if (jobs == 1) {
    worker();
  } else {
    std::vector<std::thread> pool;
    for (std::size_t j = 0; j < jobs; ++j) pool.emplace_back(worker);
    for (auto& t : pool) t.join();
  }
  out.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (error) std::rethrow_exception(error);
  out.push_failures = push_failures;
  return out;
}

}  // namespace bcc
