// Every algorithm x every trace x both buffers: no crash, conservation holds (Experiment::finish checks it).
#include <gtest/gtest.h>

#include <filesystem>
#include <map>

#include "harness/config.hpp"
#include "harness/matrix.hpp"
#include "harness/results.hpp"
#include "harness/runner.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

namespace {
const Trace& traceNamed(const std::string& name) {
  static std::map<std::string, Trace> cache;
  auto it = cache.find(name);
  if (it == cache.end()) it = cache.emplace(name, Trace::load(resolveTracePath(name))).first;
  return it->second;
}
bool available(const std::string& name) {
  try {
    resolveTracePath(name);
    return true;
  } catch (const std::runtime_error&) {
    return false;
  }
}
}  // namespace

class EveryTrace : public ::testing::TestWithParam<std::string> {};

TEST_P(EveryTrace, RunsOnEveryAvailableTraceAndBufferWithConservation) {
  const std::string algo = GetParam();
  int runs = 0;
  for (const auto& name : allTraceNames()) {
    if (!available(name)) continue;  // real traces are fetched, not vendored
    for (std::uint32_t buf : {100U, 1000U}) {
      RunConfig c;
      c.algos = {algo};
      c.trace = name;
      c.buffer_pkts = buf;
      c.duration = fromSec(4);
      RunResult r;
      ASSERT_NO_THROW(r = runOnce(c, traceNamed(name))) << algo << " " << name << " " << buf;
      EXPECT_GT(r.summary.delivered_bytes, 0u) << algo << " " << name << " " << buf;
      EXPECT_GT(r.sent_pkts, 0u);
      ++runs;
    }
  }
  EXPECT_GE(runs, 4 * 2);
}
INSTANTIATE_TEST_SUITE_P(Algorithms, EveryTrace,
                         ::testing::Values("reno", "cubic", "bbr_lite", "gcc_lite", "budgetcc"));

TEST(AllAlgorithms, VideoModeRunsForEveryAlgorithm) {
  for (const auto& algo : congestionControlNames()) {
    RunConfig c;
    c.algos = {algo};
    c.mode = "video";
    c.trace = "step_down";
    c.buffer_pkts = 1000;
    c.duration = fromSec(8);
    Experiment e(c, traceNamed("step_down"));
    e.runUntil(c.duration);
    RunResult r;
    ASSERT_NO_THROW(r = e.finish()) << algo;
    EXPECT_GT(r.summary.delivered_bytes, 0u) << algo;
    // 30 fps: after 8 s about 240 frames were generated and each becomes at least one packet
    EXPECT_GE(r.sent_pkts, 200u) << algo;
  }
}

TEST(AllAlgorithms, TwoIdenticalCubicFlowsShareAConstantLinkFairly) {
  static const Trace t = Trace::parse(bcc_test::constantTrace(12.0, 60000), "c12");
  RunConfig c;
  c.algos = {"cubic", "cubic"};
  c.mode = "fairness";
  c.trace = "c12";
  c.buffer_pkts = 1000;
  // Cubic needs a few loss cycles on a deep buffer to equalise: 0.971 after 60 s, 0.995 after 120 s.
  c.duration = fromSec(120);
  const RunResult r = runOnce(c, t);
  ASSERT_TRUE(r.summary.jain.has_value());
  EXPECT_NEAR(*r.summary.jain, 1.0, 0.02);
  c.duration = fromSec(60);
  EXPECT_GE(*runOnce(c, t).summary.jain, 0.95);
}

TEST(AllAlgorithms, MatrixSmokeRunProducesValidResultsFile) {
  MatrixOptions mo;
  mo.phase = "p1";
  mo.duration = fromSec(2);
  mo.limit = 30;  // the first 30 baseline runs
  int done = 0;
  mo.on_done = [&](const RunConfig&, const RunResult&) { ++done; };
  std::vector<std::string> entries;
  MatrixResult m;
  try {
    m = runMatrix(mo);
  } catch (const std::runtime_error& e) {
    GTEST_SKIP() << "real traces not fetched: " << e.what();
  }
  EXPECT_EQ(done, 30);
  for (const auto& r : m.runs) entries.push_back(runResultToJson(r));
  const std::string file = resultsFileJson(ResultsHeader{"p1", m.runtime_s}, entries);
  EXPECT_NE(file.find("\"runs\":[{"), std::string::npos);
  EXPECT_GE(m.runtime_s, 0.0);
}
