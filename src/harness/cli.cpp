#include "harness/cli.hpp"

#include <CLI/CLI.hpp>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "harness/config.hpp"
#include "harness/results.hpp"
#include "harness/runner.hpp"
#include "link/trace_loader.hpp"

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
  const RunResult r = runOnce(o.cfg, trace);
  const std::string json = runResultToJson(r);
  out << json << "\n";
  if (!o.out_file.empty()) {
    std::ofstream f(o.out_file, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + o.out_file);
    f << json << "\n";
  }
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
  run->add_option("--out", ro.out_file, "also write the JSON to this file");
  run->add_option("--cc-param", ro.cc_params, "controller override key=value; repeatable");

  try {
    std::vector<std::string> rev(args.rbegin(), args.rend() - 1);  // CLI11 wants argv reversed, without argv[0]
    app.parse(std::move(rev));
    if (run->parsed()) return doRun(ro, out);
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
