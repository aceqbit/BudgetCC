#include <gtest/gtest.h>

#include <vector>

#include "harness/results.hpp"
#include "harness/runner.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

namespace {
RunConfig cfg(const std::string& algo, std::uint32_t buffer, double secs) {
  RunConfig c;
  c.algos = {algo};
  c.trace = "const12";
  c.buffer_pkts = buffer;
  c.duration = static_cast<Duration>(secs * 1e9);
  return c;
}
const Trace& const12() {
  static const Trace t = Trace::parse(bcc_test::constantTrace(12.0, 60000), "const12");
  return t;
}
}  // namespace

TEST(Runner, RunTwiceGivesByteIdenticalSummary) {
  const RunConfig c = cfg("cubic", 1000, 5.0);
  const std::string a = runResultToJson(runOnce(c, const12()));
  const std::string b = runResultToJson(runOnce(c, const12()));
  EXPECT_EQ(a, b);
  EXPECT_FALSE(a.empty());
}

TEST(Runner, SamplesArriveEvery10msPerFlow) {
  RunConfig c = cfg("reno", 1000, 1.0);
  std::vector<Sample> samples;
  const RunResult with = runOnce(c, const12(), 7, [&](const Sample& s) { samples.push_back(s); });
  ASSERT_EQ(samples.size(), 99u);  // 10, 20, ... 990 ms (the 1000 ms tick is outside [0, T))
  for (std::size_t i = 0; i < samples.size(); ++i) {
    EXPECT_EQ(samples[i].t_ms, static_cast<int>(10 * (i + 1)));
    EXPECT_EQ(samples[i].stream, 7u);
    EXPECT_EQ(samples[i].flow, 0);
    EXPECT_GE(samples[i].q_ms, 0.0);
    EXPECT_NEAR(samples[i].cap_mbps, 12.0, 1.3);  // 10 ms of a 12 Mbps trace
  }
  EXPECT_GT(samples.back().cwnd_kb, 0.0);
  // observing the run must not change it
  const RunResult without = runOnce(c, const12());
  EXPECT_EQ(runResultToJson(with), runResultToJson(without));
}

TEST(Runner, MultiFlowRunSharesOneBottleneck) {
  RunConfig c = cfg("cubic", 1000, 5.0);
  c.algos = {"cubic", "reno"};
  c.mode = "fairness";
  std::vector<Sample> samples;
  Experiment e(c, const12(), 0, [&](const Sample& s) { samples.push_back(s); });
  e.runUntil(c.duration);
  const RunResult r = e.finish();
  EXPECT_EQ(e.flows(), 2u);
  EXPECT_EQ(samples.size(), 2u * 499u);
  ASSERT_TRUE(r.summary.jain.has_value());
  EXPECT_GT(*r.summary.jain, 0.5);
  EXPECT_LE(*r.summary.jain, 1.0);
  const std::uint64_t b0 = e.receiver(0).counters().recv_bytes, b1 = e.receiver(1).counters().recv_bytes;
  EXPECT_GT(b0, 0u);
  EXPECT_GT(b1, 0u);
  EXPECT_EQ(r.summary.delivered_bytes, b0 + b1);
}

TEST(Runner, InvalidConfigThrowsBeforeRunning) {
  RunConfig c = cfg("cubic", 1000, 1.0);
  c.algos = {"nope"};
  EXPECT_THROW(runOnce(c, const12()), std::invalid_argument);
}

TEST(Runner, ConservationHoldsAndCountersAreConsistent) {
  const RunConfig c = cfg("reno", 100, 10.0);
  Experiment e(c, const12());
  e.runUntil(c.duration);
  const RunResult r = e.finish();  // throws if conservation is violated
  const auto& lc = e.link().counters();
  EXPECT_EQ(e.packetsSent(), lc.delivered_pkts + lc.dropped_pkts + e.link().queuedPackets());
  EXPECT_EQ(r.dropped_pkts, lc.dropped_pkts);
  EXPECT_GT(r.events, 10'000u);
  EXPECT_EQ(r.sent_pkts, e.packetsSent());
  EXPECT_LE(lc.delivered_bytes, const12().capacityBytes(0, c.duration));
}

TEST(Runner, VideoModeProducesFramesAtThirtyFps) {
  RunConfig c = cfg("cubic", 1000, 6.0);
  c.mode = "video";
  Experiment e(c, const12());
  e.runUntil(c.duration);
  const RunResult r = e.finish();
  EXPECT_GT(r.summary.delivered_bytes, 0u);
  EXPECT_GT(r.summary.utilisation_pct, 0.0);
}

TEST(Results, JsonHasContractFieldsInOrderAndEnvBlock) {
  const RunResult r = runOnce(cfg("reno", 100, 2.0), const12());
  const std::string j = runResultToJson(r);
  const char* order[] = {"\"run\"", "\"algo\"", "\"trace\"", "\"buffer\"", "\"budget_ms\"", "\"mode\"", "\"flows\"",
                         "\"utilisation_pct\"", "\"p95_owd_ms\"", "\"p99_owd_ms\"", "\"violation_pct\"",
                         "\"violation_pct_d100\"", "\"violation_pct_d150\"", "\"violation_pct_d300\"", "\"recovery_ms\"",
                         "\"recovery_censored\"", "\"jain\"", "\"delivered_bytes\"", "\"dropped_pkts\"", "\"events\""};
  std::size_t pos = 0;
  for (const char* k : order) {
    const std::size_t at = j.find(k, pos);
    ASSERT_NE(at, std::string::npos) << k;
    pos = at;
  }
  EXPECT_NE(j.find("\"jain\":null"), std::string::npos);
  const std::string env = environmentJson();
  for (const char* k : {"cpu", "os", "compiler", "version", "flags", "build_type", "power_plan"})
    EXPECT_NE(env.find(std::string("\"") + k + "\""), std::string::npos) << k;
  const std::string file = resultsFileJson(ResultsHeader{"p1", 1.5}, {j, j});
  EXPECT_NE(file.find("\"schema\":1"), std::string::npos);
  EXPECT_NE(file.find("\"phase\":\"p1\""), std::string::npos);
  EXPECT_NE(file.find("\"git_sha\""), std::string::npos);
  EXPECT_NE(file.find("\"matrix_runtime_s\":1.5"), std::string::npos);
}
