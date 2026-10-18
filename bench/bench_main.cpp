#include <CLI/CLI.hpp>

#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "bench_core.hpp"
#include "core/json.hpp"
#include "harness/config.hpp"
#include "telemetry/loki_pusher.hpp"

namespace {
// matrix_runtime_s (M10) of a results file written by "budgetcc matrix".
double matrixRuntime(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot read " + path);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const std::string key = "\"matrix_runtime_s\":";
  const auto pos = text.find(key);
  if (pos == std::string::npos) throw std::runtime_error(path + " has no matrix_runtime_s");
  return std::stod(text.substr(pos + key.size()));
}
}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"BudgetCC engineering benchmark (M7-M10)"};
  bcc::BenchOptions opt;
  std::string out_file, loki_url, phase = "p1", matrix_json, history, change, sha;
  double duration_s = 60.0;
  app.add_option("--out", out_file, "bench JSON file")->required();
  app.add_option("--phase", phase, "p1|p2 label");
  app.add_option("--trace", opt.trace, "trace for M7/M9 (default: first real trace)");
  app.add_option("--duration-s", duration_s, "simulated seconds per M7 run");
  app.add_option("--repeats", opt.repeats, "runs per measurement (median is reported)");
  app.add_option("--matrix-json", matrix_json, "results file whose matrix_runtime_s becomes M10");
  app.add_option("--history", history, "append one line to this bench_history.jsonl");
  app.add_option("--change", change, "change id for the history line (e.g. P1, E1)");
  app.add_option("--git-sha", sha, "commit the measured code belongs to (default: the sha compiled in)");
  app.add_option("--loki", loki_url, "push the bench entry to Loki (kind=summary, mode=bench)");
  CLI11_PARSE(app, argc, argv);
  try {
    opt.duration = static_cast<bcc::Duration>(duration_s * 1e9);
    const bcc::BenchResult r = bcc::runBench(opt);
    const double m10 = matrix_json.empty() ? 0.0 : matrixRuntime(matrix_json);
    const std::string json = bcc::benchJson(phase, r, m10, sha);
    std::ofstream(out_file, std::ios::binary) << json << "\n";
    if (!history.empty()) std::ofstream(history, std::ios::app | std::ios::binary) << bcc::historyLine(change, r, m10, sha) << "\n";
    if (!loki_url.empty()) {
      bcc::LokiConfig lc;
      lc.url = loki_url;
      bcc::TelemetrySink sink(lc);
      bcc::Labels labels = {{"app", "budgetcc"}, {"kind", "summary"}, {"algo", opt.algo}, {"trace", opt.trace.empty() ? bcc::realTraceNames().at(0) : opt.trace},
                            {"phase", phase},    {"buffer", "1000"},   {"budget", "none"}, {"mode", "bench"}};
      const auto id = sink.addStream(labels, labels);
      sink.summary(id, bcc::historyLine(change.empty() ? phase : change, r, m10), 0);
      sink.flush();
      if (sink.counters().push_failures > 0) throw std::runtime_error("Loki push failed");
    }
    std::cout << json << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
