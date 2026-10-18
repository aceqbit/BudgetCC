#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "cc/budgetcc.hpp"

using namespace bcc;

namespace {
BudgetCc make(double d_ms = 150.0, const std::map<std::string, double>& params = {}) {
  static std::map<std::string, double> keep;
  keep = params;
  ParamReader p(keep, "budgetcc");
  return BudgetCc(p, d_ms);
}
// An ACK for a packet received at `recv`, delivered one return delay (20 ms) later.
AckInfo ack(Time recv, double owd_ms, double rtt_ms = 40.0) {
  return AckInfo{recv + fromMs(20), recv - fromMs(20), recv, kWireBytes, 0, static_cast<Duration>(rtt_ms * 1e6),
                 static_cast<Duration>(owd_ms * 1e6)};
}
// 1250 B every `gap_ms` for `n` packets with constant OWD; returns the time of the last receive.
Time feed(BudgetCc& b, Time start, int n, double owd_ms, double gap_ms = 1.0) {
  Time t = start;
  for (int i = 0; i < n; ++i) {
    t += static_cast<Time>(gap_ms * 1e6);
    b.onAck(ack(t, owd_ms));
  }
  return t;
}
// Runs control ticks from `from` (exclusive) up to and including `to`.
void tick(BudgetCc& b, Time from, Time to) {
  for (Time t = from + fromMs(10); t <= to; t += fromMs(10)) b.onTimer(t);
}
}  // namespace

TEST(BudgetCc, QstarFollowsTheBudget) {
  BudgetCc b150 = make(150.0);
  feed(b150, 0, 50, 20.0);
  EXPECT_DOUBLE_EQ(b150.qstarMs(), 104.0);  // (150 - 20) x 0.8
  BudgetCc b100 = make(100.0);
  feed(b100, 0, 50, 20.0);
  EXPECT_DOUBLE_EQ(b100.qstarMs(), 64.0);
  BudgetCc tight = make(25.0);
  feed(tight, 0, 50, 20.0);
  EXPECT_DOUBLE_EQ(tight.qstarMs(), 10.0);  // floor of 10 ms
  BudgetCc wide = make(150.0, {{"margin", 0.1}});
  feed(wide, 0, 50, 20.0);
  EXPECT_DOUBLE_EQ(wide.qstarMs(), 117.0);
}

TEST(BudgetCc, QueueDelayIsSmoothedOwdMinusMinimum) {
  BudgetCc b = make();
  const Time t = feed(b, 0, 20, 20.0);
  EXPECT_NEAR(b.queueDelayEstimateMs(), 0.0, 1e-9);
  b.onAck(ack(t + fromMs(1), 100.0));
  EXPECT_NEAR(b.queueDelayEstimateMs(), 80.0 / 8.0, 1e-9);  // EWMA with weight 1/8
}

TEST(BudgetCc, KeepsTheStartRateUntilTheFirstAck) {
  BudgetCc b = make();
  tick(b, 0, fromMs(500));
  EXPECT_DOUBLE_EQ(b.pacingRateBps(), 1e6 * 1.0);
  EXPECT_DOUBLE_EQ(b.capacityEstimateBps(), 1e6);
}

TEST(BudgetCc, CapacityRisesSlowlyAndFallsFast) {
  BudgetCc b = make(150.0, {{"start_rate_bps", 1e6}});
  // 10 Mbps of deliveries: 1250 B every 1 ms
  Time t = feed(b, 0, 300, 20.0);
  const double c0 = b.capacityEstimateBps();
  b.onTimer(t + fromMs(25));
  const double c1 = b.capacityEstimateBps();
  EXPECT_GT(c1, c0);
  EXPECT_LT(c1 - c0, 0.11 * (10e6 - c0));  // at most the 0.1 up weight of the gap
  // let it converge, then deliveries stop being slow
  for (int i = 0; i < 100; ++i) {
    t = feed(b, t, 10, 20.0);
    b.onTimer(t + fromMs(25));
  }
  EXPECT_NEAR(b.capacityEstimateBps(), 10e6, 0.7e6);
  // delivery rate halves: the estimate follows within a couple of ticks, not tens
  const double before = b.capacityEstimateBps();
  t = feed(b, t, 40, 20.0, 2.0);  // 5 Mbps
  for (int i = 0; i < 3; ++i) b.onTimer(t + fromMs(25) + fromMs(10) * i);
  EXPECT_LT(b.capacityEstimateBps(), 0.75 * before);
}

TEST(BudgetCc, CollapseFastPathResetsTheEstimate) {
  BudgetCc b = make();
  Time t = feed(b, 0, 400, 20.0);  // 10 Mbps
  for (int i = 0; i < 150; ++i) {
    t = feed(b, t, 10, 20.0);
    b.onTimer(t + fromMs(25));
  }
  const double high = b.capacityEstimateBps();
  ASSERT_GT(high, 6e6);
  // an outage: nothing is received any more; the fast window empties and C_hat drops by at least half in one tick
  b.onTimer(t + fromMs(25) + fromMs(200));
  EXPECT_LE(b.capacityEstimateBps(), 0.5 * high);
  EXPECT_GE(b.pacingRateBps(), 100e3);
}

TEST(BudgetCc, ProportionalDrainCapsRateAtCapacityTimesQstarOverQ) {
  BudgetCc b = make();
  Time t = feed(b, 0, 400, 20.0);
  for (int i = 0; i < 150; ++i) {
    t = feed(b, t, 10, 20.0);
    b.onTimer(t + fromMs(25));
  }
  // standing queue of 2 x Q*: smoothed OWD 20 + 208 ms
  for (int i = 0; i < 200; ++i) b.onAck(ack(t + fromMs(1) * (i + 1), 228.0));
  t += fromMs(200);
  ASSERT_NEAR(b.queueDelayEstimateMs(), 208.0, 1.0);
  b.onTimer(t + fromMs(25));
  EXPECT_LE(b.pacingRateBps(), b.capacityEstimateBps() * 0.5 * 1.01);
  EXPECT_NEAR(b.pacingRateBps(), b.capacityEstimateBps() * b.qstarMs() / b.queueDelayEstimateMs(), 1.0);
}

TEST(BudgetCc, GainIsClampedBetweenHalfAndTheProbeCeiling) {
  BudgetCc empty = make();
  Time t = feed(empty, 0, 400, 20.0);
  empty.onTimer(t + fromMs(25));
  EXPECT_LE(empty.pacingRateBps(), 1.15 * empty.capacityEstimateBps() * (1 + 1e-12));
  EXPECT_GT(empty.pacingRateBps(), empty.capacityEstimateBps());  // e = 1 -> 1 + 0.5 clamps to 1.15
  BudgetCc k_big = make(150.0, {{"k", 100.0}});
  t = feed(k_big, 0, 400, 20.0);
  k_big.onTimer(t + fromMs(25));
  EXPECT_LE(k_big.pacingRateBps(), 1.15 * k_big.capacityEstimateBps() * (1 + 1e-12));
}

TEST(BudgetCc, FloorAndInflightCapFormula) {
  BudgetCc b = make();
  feed(b, 0, 50, 20.0);
  for (int i = 0; i < 100; ++i) b.onAck(ack(fromMs(60) + fromMs(1) * i, 400.0));
  for (int k = 0; k < 40; ++k) b.onTimer(fromMs(300) + fromMs(100) * k);  // no deliveries: rate sinks to the floor
  EXPECT_DOUBLE_EQ(b.pacingRateBps(), 100e3);
  const double expect = 100e3 / 8.0 * (0.040 + b.qstarMs() / 1000.0);
  EXPECT_NEAR(static_cast<double>(b.inflightCapBytes()), std::max(expect, 4.0 * 1250), 1.0);
  EXPECT_DOUBLE_EQ(b.probe().qstar_ms, b.qstarMs());
  EXPECT_EQ(b.probe().chat_bps, b.capacityEstimateBps());
  EXPECT_EQ(std::string(b.name()), "budgetcc");
}

TEST(BudgetCc, BurstOfThreeLossesCutsCapacityByTwentyPercentIsolatedLossDoesNot) {
  BudgetCc b = make();
  const double c = b.capacityEstimateBps();
  b.onLoss(LossInfo{fromMs(100), 1, kWireBytes, false});
  EXPECT_DOUBLE_EQ(b.capacityEstimateBps(), c);
  b.onLoss(LossInfo{fromMs(500), 2, kWireBytes, false});  // new window: still isolated
  b.onLoss(LossInfo{fromMs(1000), 3, kWireBytes, false});
  EXPECT_DOUBLE_EQ(b.capacityEstimateBps(), c);
  BudgetCc burst = make();
  for (int i = 0; i < 3; ++i) burst.onLoss(LossInfo{fromMs(100) + fromMs(i), 1u + i, kWireBytes, false});
  EXPECT_DOUBLE_EQ(burst.capacityEstimateBps(), 0.8 * c);
  burst.onLoss(LossInfo{fromMs(104), 9, kWireBytes, false});  // a fourth loss in the same window: no second cut
  EXPECT_DOUBLE_EQ(burst.capacityEstimateBps(), 0.8 * c);
}

TEST(BudgetCc, RandomStreamsKeepEverythingFiniteAndBounded) {
  std::mt19937_64 rng(11);
  for (int run = 0; run < 5; ++run) {
    BudgetCc b = make(100.0 + 50.0 * run);
    Time t = 0;
    for (int i = 0; i < 20000; ++i) {
      t += static_cast<Time>(rng() % 3'000'000);
      b.onAck(ack(t, 20.0 + static_cast<double>(rng() % 400)));
      if (rng() % 50 == 0) b.onLoss(LossInfo{t, 1, kWireBytes, rng() % 2 == 0});
      if (rng() % 10 == 0) {
        b.onTimer(t + fromMs(static_cast<int>(rng() % 60)));
        ASSERT_TRUE(std::isfinite(b.pacingRateBps()));
        ASSERT_TRUE(std::isfinite(b.capacityEstimateBps()));
        ASSERT_GE(b.pacingRateBps(), 100e3);
        ASSERT_LE(b.pacingRateBps(), 1.15 * b.capacityEstimateBps() * (1 + 1e-9) + 1e-6);
        ASSERT_GT(b.inflightCapBytes(), 0u);
      }
    }
  }
}

TEST(BudgetCc, EmptyQueueNeverLowersTheCapacityEstimate) {
  BudgetCc b = make();
  Time t = feed(b, 0, 400, 20.0);  // 10 Mbps, flat OWD: queue estimate 0
  for (int i = 0; i < 150; ++i) {
    t = feed(b, t, 10, 20.0);
    b.onTimer(t + fromMs(25));
  }
  const double before = b.capacityEstimateBps();
  for (int i = 0; i < 20; ++i) {  // deliveries slow to 6.25 Mbps (sender-limited), queue still empty
    t = feed(b, t, 6, 20.0, 1.6);
    b.onTimer(t + fromMs(25));
  }
  EXPECT_GE(b.capacityEstimateBps(), 0.99 * before);  // slow-up only; no fast-down without a queue
}

TEST(BudgetCc, CollapsePathIsSkippedWhenTheWindowCannotHoldFourPackets) {
  BudgetCc b = make(150.0, {{"start_rate_bps", 300e3}});  // 300 kbps: < 1 packet per 20 ms
  feed(b, 0, 5, 20.0, 40.0);
  b.onTimer(fromMs(400));
  EXPECT_GE(b.capacityEstimateBps(), 0.99 * 300e3);
}
