#include <gtest/gtest.h>

#include <cmath>

#include "harness/summary.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

TEST(Summary, PercentileNearestRankEdgeCases) {
  EXPECT_EQ(percentileNearestRank({42}, 95), 42);
  EXPECT_EQ(percentileNearestRank({42}, 99), 42);
  std::vector<std::int64_t> v;
  for (int i = 1; i <= 20; ++i) v.push_back(i);
  EXPECT_EQ(percentileNearestRank(v, 95), 19);  // ceil(0.95 * 20) = 19th smallest
  EXPECT_EQ(percentileNearestRank(v, 99), 20);
  EXPECT_EQ(percentileNearestRank({5, 5, 5, 5}, 95), 5);
  std::vector<std::int64_t> h;
  for (int i = 1; i <= 100; ++i) h.push_back(i);
  EXPECT_EQ(percentileNearestRank(h, 95), 95);
  EXPECT_EQ(percentileNearestRank(h, 99), 99);
}

TEST(Summary, JainIndex) {
  EXPECT_DOUBLE_EQ(jainIndex({1.0, 1.0}), 1.0);
  EXPECT_DOUBLE_EQ(jainIndex({1.0, 0.0}), 0.5);
  EXPECT_DOUBLE_EQ(jainIndex({3.0, 1.0}), 16.0 / 20.0);
  EXPECT_DOUBLE_EQ(jainIndex({0.0, 0.0}), 1.0);
  EXPECT_NEAR(jainIndex({2.0, 2.0, 2.0, 2.0}), 1.0, 1e-12);
}

TEST(Summary, UtilisationViolationsAndPercentilesOnHandComputedData) {
  const Trace trace = Trace::parse(bcc_test::constantTrace(12.0, 10000), "c");
  std::vector<std::int64_t> owd;
  for (int i = 1; i <= 20; ++i) owd.push_back(fromMs(10 * i));  // 10, 20, ... 200 ms
  SummaryInput in;
  in.owd_ns = {&owd};
  const std::uint64_t cap = trace.capacityBytes(0, fromSec(10));
  in.flow_bytes = {cap / 2};
  in.trace = &trace;
  in.duration = fromSec(10);
  in.budget_ms = 150.0;
  const Summary s = computeSummary(in);
  EXPECT_NEAR(s.utilisation_pct, 50.0, 1e-3);
  EXPECT_DOUBLE_EQ(s.p95_owd_ms, 190.0);
  EXPECT_DOUBLE_EQ(s.p99_owd_ms, 200.0);
  EXPECT_DOUBLE_EQ(s.violation_pct, 25.0);       // 160..200 ms: 5 of 20
  EXPECT_DOUBLE_EQ(s.violation_pct_d100, 50.0);  // 110..200 ms: 10 of 20
  EXPECT_DOUBLE_EQ(s.violation_pct_d150, 25.0);
  EXPECT_DOUBLE_EQ(s.violation_pct_d300, 0.0);
  EXPECT_FALSE(s.jain.has_value());
  EXPECT_EQ(s.delivered_bytes, cap / 2);
}

TEST(Summary, ViolationUsesTheRunsBudgetAndStrictInequality) {
  const Trace trace = Trace::parse(bcc_test::constantTrace(12.0, 1000), "c");
  std::vector<std::int64_t> owd = {fromMs(100), fromMs(100), fromMs(101), fromMs(250)};
  SummaryInput in;
  in.owd_ns = {&owd};
  in.flow_bytes = {1};
  in.trace = &trace;
  in.duration = fromSec(1);
  in.budget_ms = 100.0;
  EXPECT_DOUBLE_EQ(computeSummary(in).violation_pct, 50.0);  // exactly 100 ms is not a violation
}

TEST(Summary, TwoFlowsGiveJainAndPoolPercentiles) {
  const Trace trace = Trace::parse(bcc_test::constantTrace(12.0, 1000), "c");
  std::vector<std::int64_t> a = {fromMs(20), fromMs(30)}, b = {fromMs(40)};
  SummaryInput in;
  in.owd_ns = {&a, &b};
  in.flow_bytes = {3000, 1000};
  in.trace = &trace;
  in.duration = fromSec(1);
  const Summary s = computeSummary(in);
  ASSERT_TRUE(s.jain.has_value());
  EXPECT_DOUBLE_EQ(*s.jain, 0.8);
  EXPECT_DOUBLE_EQ(s.p95_owd_ms, 40.0);
  EXPECT_EQ(s.delivered_bytes, 4000u);
}

TEST(Summary, EmptyDelaySetGivesNaNNotACrash) {
  const Trace trace = Trace::parse(bcc_test::constantTrace(12.0, 1000), "c");
  std::vector<std::int64_t> none;
  SummaryInput in;
  in.owd_ns = {&none};
  in.flow_bytes = {0};
  in.trace = &trace;
  in.duration = fromSec(1);
  const Summary s = computeSummary(in);
  EXPECT_TRUE(std::isnan(s.p95_owd_ms));
  EXPECT_TRUE(std::isnan(s.violation_pct));
  EXPECT_DOUBLE_EQ(s.utilisation_pct, 0.0);
}

namespace {
std::vector<double> queueSeries(int duration_s, double hi_from_s, double hi_to_s, double hi) {
  std::vector<double> q(static_cast<std::size_t>(duration_s) * 100, 0.0);
  for (std::size_t k = 0; k < q.size(); ++k) {
    const double t = static_cast<double>(k + 1) * 0.01;
    if (t >= hi_from_s && t < hi_to_s) q[k] = hi;
  }
  return q;
}
}  // namespace

TEST(Summary, RecoveryOnSyntheticStep) {
  const Trace trace = Trace::parse(bcc_test::rateTrace({{0, 12.0}, {5000, 3.0}}, 20000), "step");
  const auto q = queueSeries(20, 5.0, 7.0, 200.0);
  const Recovery r = recoveryStats(trace, q, fromSec(20), 104.0);
  EXPECT_EQ(r.events, 1u);
  EXPECT_EQ(r.censored, 0u);
  ASSERT_TRUE(r.median_ms.has_value());
  // event at 4.94 s (first t whose [t, t+200ms) mean capacity is <= half of the previous second's);
  // queue back under Q* from the sample at 7.0 s
  EXPECT_NEAR(*r.median_ms, 7000.0 - 4940.0, 1e-6);
}

TEST(Summary, RecoveryCensoredWhenQueueNeverDrains) {
  const Trace trace = Trace::parse(bcc_test::rateTrace({{0, 12.0}, {5000, 3.0}}, 20000), "step");
  const auto q = queueSeries(20, 5.0, 25.0, 200.0);
  const Recovery r = recoveryStats(trace, q, fromSec(20), 104.0);
  EXPECT_EQ(r.events, 1u);
  EXPECT_EQ(r.censored, 1u);
  EXPECT_NEAR(*r.median_ms, 20000.0 - 4940.0, 1e-6);  // run end minus event time
}

TEST(Summary, RecoveryZeroWhenQueueNeverExceedsTarget) {
  const Trace trace = Trace::parse(bcc_test::rateTrace({{0, 12.0}, {5000, 3.0}}, 20000), "step");
  const std::vector<double> q(2000, 50.0);
  const Recovery r = recoveryStats(trace, q, fromSec(20), 104.0);
  EXPECT_EQ(r.events, 1u);
  EXPECT_DOUBLE_EQ(*r.median_ms, 0.0);
  EXPECT_EQ(r.censored, 0u);
}

TEST(Summary, NoCapacityDropMeansNoRecoveryValue) {
  const Trace trace = Trace::parse(bcc_test::constantTrace(12.0, 20000), "c");
  const std::vector<double> q(2000, 500.0);
  const Recovery r = recoveryStats(trace, q, fromSec(20), 104.0);
  EXPECT_EQ(r.events, 0u);
  EXPECT_FALSE(r.median_ms.has_value());
}

TEST(Summary, DropEventsLessThanTwoSecondsApartMerge) {
  // 12 -> 6 -> 3 Mbps steps 1 s apart are one event; a drop 5 s later is a second.
  const Trace trace =
      Trace::parse(bcc_test::rateTrace({{0, 24.0}, {5000, 8.0}, {6000, 2.0}, {11000, 0.5}}, 20000), "multi");
  const std::vector<double> q(2000, 0.0);
  const Recovery r = recoveryStats(trace, q, fromSec(20), 104.0);
  EXPECT_EQ(r.events, 2u);
}
