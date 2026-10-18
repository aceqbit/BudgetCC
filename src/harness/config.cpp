#include "harness/config.hpp"

#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "cc/cc_interface.hpp"

namespace bcc {

std::string RunConfig::runId() const { return fmt::format("r{:04d}", run_index); }

std::string RunConfig::algoLabel() const {
  std::string s;
  for (std::size_t i = 0; i < algos.size(); ++i) {
    if (i != 0) s += '+';
    s += algos[i];
  }
  return s;
}

bool RunConfig::usesBudget() const {
  for (const auto& a : algos)
    if (a == "budgetcc") return true;
  return false;
}

std::string RunConfig::budgetLabel() const { return usesBudget() ? fmt::format("{}", budget_ms) : "none"; }

double RunConfig::recoveryQstarMs() const { return (150.0 - toMs(prop_delay)) * 0.8; }

void normalize(RunConfig& c) {
  if (c.algos.size() > 1 && c.mode == "bulk") c.mode = "fairness";
}

void validate(const RunConfig& c) {
  auto bad = [](const std::string& m) { throw std::invalid_argument(m); };
  if (c.algos.empty()) bad("at least one --algo is required");
  const auto& known = congestionControlNames();
  for (const auto& a : c.algos) {
    bool ok = false;
    for (const auto& k : known) ok |= (k == a);
    if (!ok) bad("unknown algorithm '" + a + "'");
  }
  if (c.mode != "bulk" && c.mode != "video" && c.mode != "fairness") bad("mode must be bulk, video or fairness");
  if (c.mode == "video" && c.algos.size() != 1) bad("video mode supports exactly one flow");
  if (c.mode == "fairness" && c.algos.size() < 2) bad("fairness mode needs at least two flows");
  if (c.trace.empty()) bad("trace is required");
  if (c.buffer_pkts == 0) bad("buffer must be at least 1 packet");
  if (!(c.budget_ms > 0.0)) bad("budget-ms must be positive");
  if (c.prop_delay <= 0) bad("prop-delay-ms must be positive");
  if (c.duration < fromMs(100)) bad("duration-s must be at least 0.1");
  if (c.phase != "p1" && c.phase != "p2") bad("phase must be p1 or p2");
}

const std::vector<std::string>& realTraceNames() {
  static const std::vector<std::string> n = {"att_lte_driving",     "att_lte_driving_2016", "tmobile_lte_driving",
                                             "verizon_lte_driving", "tmobile_lte_short",    "verizon_lte_short"};
  return n;
}

const std::vector<std::string>& syntheticTraceNames() {
  static const std::vector<std::string> n = {"step_down", "step_up", "sawtooth", "outage_2s"};
  return n;
}

std::vector<std::string> allTraceNames() {
  std::vector<std::string> all = realTraceNames();
  all.insert(all.end(), syntheticTraceNames().begin(), syntheticTraceNames().end());
  return all;
}

std::vector<RunConfig> expandMatrix(const std::string& phase) {
  std::vector<RunConfig> runs;
  const auto traces = allTraceNames();
  auto add = [&](std::vector<std::string> algos, const std::string& trace, std::uint32_t buffer, double budget,
                 const std::string& mode) {
    RunConfig c;
    c.algos = std::move(algos);
    c.trace = trace;
    c.buffer_pkts = buffer;
    c.budget_ms = budget;
    c.mode = mode;
    c.phase = phase;
    c.run_index = static_cast<std::uint32_t>(runs.size());
    runs.push_back(std::move(c));
  };
  for (const char* algo : {"reno", "cubic", "bbr_lite", "gcc_lite"})
    for (std::uint32_t buf : {100U, 1000U})
      for (const auto& t : traces) add({algo}, t, buf, 150.0, "bulk");
  for (double d : {100.0, 150.0, 300.0})
    for (std::uint32_t buf : {100U, 1000U})
      for (const auto& t : traces) add({"budgetcc"}, t, buf, d, "bulk");
  for (const char* algo : {"reno", "cubic", "bbr_lite", "gcc_lite", "budgetcc"})
    for (const auto& t : traces) add({algo}, t, 1000U, 150.0, "video");
  const std::vector<std::vector<std::string>> pairs = {{"reno", "reno"},         {"cubic", "cubic"},
                                                       {"bbr_lite", "bbr_lite"}, {"gcc_lite", "gcc_lite"},
                                                       {"budgetcc", "budgetcc"}, {"budgetcc", "cubic"}};
  for (const auto& p : pairs)
    for (const auto& t : traces) add(p, t, 1000U, 150.0, "fairness");
  return runs;
}

std::string resolveTracePath(const std::string& name_or_path) {
  namespace fs = std::filesystem;
  std::vector<fs::path> tried;
  auto probe = [&](const fs::path& p) {
    tried.push_back(p);
    return fs::is_regular_file(p);
  };
  if (probe(name_or_path)) return name_or_path;
  std::vector<fs::path> roots;
  if (const char* env = std::getenv("BUDGETCC_TRACES")) roots.emplace_back(env);
  roots.emplace_back("traces");
#ifdef BUDGETCC_SOURCE_DIR
  roots.emplace_back(fs::path(BUDGETCC_SOURCE_DIR) / "traces");
#endif
  for (const auto& r : roots)
    for (const char* sub : {"synthetic", "real"}) {
      const fs::path p = r / sub / (name_or_path + ".trace");
      if (probe(p)) return p.string();
    }
  std::string msg = "trace '" + name_or_path + "' not found; tried:";
  for (const auto& p : tried) msg += " " + p.string();
  throw std::runtime_error(msg);
}

}  // namespace bcc
