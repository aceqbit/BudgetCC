// BudgetCC v0 (Phase 1 threshold AIMD). Replaced by the full controller in Phase 2 (A1).
#include <gtest/gtest.h>

#include <cmath>

#include "cc/budgetcc.hpp"

using namespace bcc;

namespace {
BudgetCc make(double d_ms = 150.0) {
  static const std::map<std::string, double> none;
  ParamReader p(none, "budgetcc");
  return BudgetCc(p, d_ms);
}
AckInfo ack(Time now, double owd_ms, double rtt_ms = 40.0) {
  return AckInfo{now, now - fromMs(40), now, kWireBytes, 0, static_cast<Duration>(rtt_ms * 1e6),
                 static_cast<Duration>(owd_ms * 1e6)};
}
void feed(BudgetCc& b, int n, double owd_ms, Time& now) {
  for (int i = 0; i < n; ++i) {
    now += fromMs(1);
    b.onAck(ack(now, owd_ms));
  }
}
}  // namespace

TEST(BudgetCcV0, QstarFollowsTheBudget) {
  Time now = 0;
  BudgetCc b150 = make(150.0);
  feed(b150, 50, 20.0, now);
  EXPECT_DOUBLE_EQ(b150.qstarMs(), 104.0);  // (150 - 20) x 0.8
  BudgetCc b100 = make(100.0);
  now = 0;
  feed(b100, 50, 20.0, now);
  EXPECT_DOUBLE_EQ(b100.qstarMs(), 64.0);
  BudgetCc tight = make(25.0);
  now = 0;
  feed(tight, 50, 20.0, now);
  EXPECT_DOUBLE_EQ(tight.qstarMs(), 10.0);  // floor of 10 ms
}

TEST(BudgetCcV0, QueueDelayIsSmoothedOwdMinusMinimum) {
  BudgetCc b = make();
  Time now = 0;
  feed(b, 20, 20.0, now);
  EXPECT_NEAR(b.queueDelayEstimateMs(), 0.0, 1e-9);
  now += fromMs(1);
  b.onAck(ack(now, 100.0));
  EXPECT_NEAR(b.queueDelayEstimateMs(), 80.0 / 8.0, 1e-9);  // EWMA with weight 1/8
}

TEST(BudgetCcV0, AddsOnePacketPerRttBelowTarget) {
  BudgetCc b = make();
  Time now = 0;
  feed(b, 50, 20.0, now);  // srtt = 40 ms
  const double r0 = b.pacingRateBps();
  for (int k = 0; k < 4; ++k) b.onTimer(now + (k + 1) * fromMs(10));  // one RTT of ticks
  EXPECT_NEAR(b.pacingRateBps() - r0, 1250.0 * 8.0 / 0.04, 1.0);   // + one packet per RTT
}

TEST(BudgetCcV0, MultipliesByPointEightAboveTargetOncePerRtt) {
  BudgetCc b = make();
  Time now = 0;
  feed(b, 50, 20.0, now);
  for (int i = 0; i < 100; ++i) {
    now += fromMs(1);
    b.onAck(ack(now, 400.0));
  }
  ASSERT_GT(b.queueDelayEstimateMs(), b.qstarMs());
  const double r0 = b.pacingRateBps();
  b.onTimer(now + fromMs(1));
  EXPECT_NEAR(b.pacingRateBps(), 0.8 * r0, 1e-6);
  b.onTimer(now + fromMs(11));  // same RTT: no second cut
  EXPECT_NEAR(b.pacingRateBps(), 0.8 * r0, 1e-6);
  b.onTimer(now + fromMs(60));  // a full srtt later
  EXPECT_NEAR(b.pacingRateBps(), 0.64 * r0, 1e-6);
}

TEST(BudgetCcV0, FloorAndInflightCap) {
  BudgetCc b = make();
  Time now = 0;
  feed(b, 50, 20.0, now);
  for (int i = 0; i < 100; ++i) {
    now += fromMs(1);
    b.onAck(ack(now, 400.0));
  }
  for (int k = 0; k < 40; ++k) b.onTimer(now + fromMs(50) * (k + 1));
  EXPECT_DOUBLE_EQ(b.pacingRateBps(), 100e3);  // floor
  const double expect = 100e3 / 8.0 * (0.040 + 0.104);
  EXPECT_NEAR(static_cast<double>(b.inflightCapBytes()), std::max(expect, 4.0 * 1250), 1.0);
  EXPECT_EQ(b.probe().chat_bps, b.pacingRateBps());
  EXPECT_DOUBLE_EQ(b.probe().qstar_ms, 104.0);
  EXPECT_EQ(std::string(b.name()), "budgetcc");
}

TEST(BudgetCcV0, LossDoesNotChangeTheRate) {
  BudgetCc b = make();
  const double r = b.pacingRateBps();
  b.onLoss(LossInfo{fromMs(5), 1, kWireBytes, false});
  EXPECT_DOUBLE_EQ(b.pacingRateBps(), r);
}
