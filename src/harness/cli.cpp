#include "harness/cli.hpp"

#include <CLI/CLI.hpp>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "harness/config.hpp"
#include "harness/instrumented.hpp"
#include "harness/matrix.hpp"
#include "harness/results.hpp"
#include "harness/runner.hpp"
#include "link/trace_loader.hpp"
#include "telemetry/csv_writer.hpp"

namespace bcc {

namespace {

std::map<std::string, double> parseCcParams(const std::vector<std::string>& kv) {
  std::map<std::string, double> m;
  for (const auto& s : kv) {
    const auto eq = s.find('=');
    if (eq == std::string::npos || eq == 0) throw std::invalid_argument("--cc-param expects key=value, got '" + s + "'");
    const std::string v = s.substr(eq + 1);
    double d = 0.0;
    const auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), d);
    if (ec != std::errc() || p != v.data() + v.size()) throw std::invalid_argument("bad number in --cc-param '" + s + "'");
    m[s.substr(0, eq)] = d;
  }
  return m;
}

struct RunOptions {
  RunConfig cfg;
  std::vector<std::string> cc_params;
  double duration_s = 60.0;
  double prop_delay_ms = 20.0;
  std::string out_file;
};

int doRun(RunOptions& o, std::ostream& out) {
  o.cfg.cc_params = parseCcParams(o.cc_params);
  o.cfg.duration = static_cast<Duration>(o.duration_s * 1e9);
  o.cfg.prop_delay = static_cast<Duration>(o.prop_delay_ms * 1e6);
  normalize(o.cfg);
  validate(o.cfg);
  const Trace trace = Trace::load(resolveTracePath(o.cfg.trace));
  std::unique_ptr<CsvWriter> csv;
  if (!o.cfg.csv_dir.empty()) {
    std::filesystem::create_directories(o.cfg.csv_dir);
    csv = std::make_unique<CsvWriter>((std::filesystem::path(o.cfg.csv_dir) /
                                       (o.cfg.runId() + "_" + o.cfg.algoLabel() + "_" + o.cfg.trace + "_" +
                                        std::to_string(o.cfg.buffer_pkts) + ".csv"))
                                          .string());
  }
  std::unique_ptr<TelemetrySink> loki;
  if (!o.cfg.loki_url.empty()) {
    LokiConfig lc;
    lc.url = o.cfg.loki_url;
    loki = std::make_unique<TelemetrySink>(lc);
  }
  const RunResult r = runInstrumented(o.cfg, trace, loki.get(), csv.get());
  const std::string json = runResultToJson(r);
  out << json << "\n";
  if (!o.out_file.empty()) {
    std::ofstream f(o.out_file, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + o.out_file);
    f << json << "\n";
  }
  if (loki && loki->counters().push_failures > 0)
    throw std::runtime_error("Loki push failed; is " + o.cfg.loki_url + " reachable?");
  return 0;
}

struct MatrixCliOptions {
  std::string phase = "p1";
  std::string out_file;
  double duration_s = 0.0;
  std::size_t limit = 0;
  std::string loki_url;
};

int doMatrix(const MatrixCliOptions& o, std::ostream& err) {
  if (o.phase != "p1" && o.phase != "p2") throw std::invalid_argument("phase must be p1 or p2");
  MatrixOptions mo;
  mo.phase = o.phase;
  mo.duration = static_cast<Duration>(o.duration_s * 1e9);
  mo.limit = o.limit;
  std::unique_ptr<TelemetrySink> loki;
  if (!o.loki_url.empty()) {
    LokiConfig lc;
    lc.url = o.loki_url;
    loki = std::make_unique<TelemetrySink>(lc);
    mo.loki = loki.get();
  }
  std::size_t done = 0;
  mo.on_done = [&](const RunConfig& c, const RunResult&) {
    err << "[" << ++done << "] " << c.runId() << " " << c.algoLabel() << " " << c.trace << " buffer=" << c.buffer_pkts
        << " mode=" << c.mode << "\n";
  };
  const MatrixResult m = runMatrix(mo);
  std::vector<std::string> entries;
  for (const RunResult& r : m.runs) entries.push_back(runResultToJson(r));
  const std::string json = resultsFileJson(ResultsHeader{o.phase, m.runtime_s}, entries);
  std::ofstream f(o.out_file, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + o.out_file);
  f << json << "\n";
  if (loki && loki->counters().push_failures > 0) throw std::runtime_error("Loki push failed");
  err << m.runs.size() << " runs in " << m.runtime_s << " s -> " << o.out_file << "\n";
  return 0;
}

}  // namespace

int runMain(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  CLI::App app{"BudgetCC: trace-driven congestion-control testbed"};
  app.require_subcommand(1);

  RunOptions ro;
  CLI::App* run = app.add_subcommand("run", "run one experiment and print its summary as JSON");
  run->add_option("--algo", ro.cfg.algos, "reno|cubic|bbr_lite|gcc_lite|budgetcc; repeat for several flows")->required();
  run->add_option("--trace", ro.cfg.trace, "trace name or path")->required();
  run->add_option("--buffer", ro.cfg.buffer_pkts, "bottleneck buffer in packets")->required();
  run->add_option("--budget-ms", ro.cfg.budget_ms, "one-way delay budget D");
  run->add_option("--mode", ro.cfg.mode, "bulk|video");
  run->add_option("--prop-delay-ms", ro.prop_delay_ms, "one-way propagation delay");
  run->add_option("--duration-s", ro.duration_s, "simulated seconds");
  run->add_option("--seed", ro.cfg.seed, "seed for flow start jitter");
  run->add_option("--phase", ro.cfg.phase, "p1|p2 (label only)");
  run->add_option("--loki", ro.cfg.loki_url, "push samples and the summary to this Loki base URL");
  run->add_option("--csv", ro.cfg.csv_dir, "write the 10 ms samples to a CSV file in this directory");
  run->add_option("--out", ro.out_file, "also write the JSON to this file");
  run->add_option("--cc-param", ro.cc_params, "controller override key=value; repeatable");

  MatrixCliOptions mx;
  CLI::App* matrix = app.add_subcommand("matrix", "run the full 250-run experiment matrix");
  matrix->add_option("--phase", mx.phase, "p1|p2")->required();
  matrix->add_option("--out", mx.out_file, "results JSON file")->required();
  matrix->add_option("--loki", mx.loki_url, "push samples and summaries to this Loki base URL");
  matrix->add_option("--duration-s", mx.duration_s, "override the 60 s run length (smoke tests)");
  matrix->add_option("--limit", mx.limit, "only the first N runs (smoke tests)");

  try {
    std::vector<std::string> rev(args.rbegin(), args.rend() - 1);  // CLI11 wants argv reversed, without argv[0]
    app.parse(std::move(rev));
    if (run->parsed()) return doRun(ro, out);
    if (matrix->parsed()) return doMatrix(mx, err);
  } catch (const CLI::ParseError& e) {
    return app.exit(e, out, err) == 0 ? 0 : 2;
  } catch (const std::invalid_argument& e) {
    err << "error: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    err << "error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

}  // namespace bcc
