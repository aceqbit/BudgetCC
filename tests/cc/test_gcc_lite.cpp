#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "cc/gcc_lite.hpp"

using namespace bcc;

namespace {
GccLite make() {
  static const std::map<std::string, double> none;
  ParamReader p(none, "gcc_lite");
  return GccLite(p);
}
// A packet sent every `gap` whose one-way delay is owd(i); acked 20 ms after arrival.
AckInfo ack(int i, Duration gap, Duration owd) {
  const Time send = i * gap;
  const Time recv = send + owd;
  return AckInfo{recv + fromMs(20), send, recv, kWireBytes, static_cast<std::uint64_t>(i), owd + fromMs(20), owd};
}
}  // namespace

TEST(GccLite, FlatDelayIsNeverOveruse) {
  GccLite g = make();
  for (int i = 0; i < 2000; ++i) {
    g.onAck(ack(i, fromMs(1), fromMs(30)));
    ASSERT_NE(g.usage(), GccLite::Usage::Overusing) << i;
  }
  EXPECT_EQ(g.usage(), GccLite::Usage::Normal);
}

TEST(GccLite, RisingDelayIsDetectedAsOveruse) {
  GccLite g = make();
  bool over = false;
  for (int i = 0; i < 400 && !over; ++i) {
    g.onAck(ack(i, fromMs(1), fromMs(30) + i * fromUs(500)));  // queue grows by 0.5 ms per packet
    over = g.usage() == GccLite::Usage::Overusing;
  }
  EXPECT_TRUE(over);
}

TEST(GccLite, DecreaseToEightyFivePercentOfIncomingThenHoldAndRegrow) {
  GccLite g = make();
  Time now = 0;
  // 0.5 Mbps incoming (one 1250 B packet every 20 ms) while the queue grows by 3 ms per packet
  for (int i = 0; i < 200; ++i) {
    const AckInfo a = ack(i, fromMs(20), fromMs(30) + i * fromMs(3));
    g.onAck(a);
    now = a.now;
    if (g.usage() == GccLite::Usage::Overusing) break;
  }
  ASSERT_EQ(g.usage(), GccLite::Usage::Overusing);
  const double before = g.delayRateBps();
  g.onTimer(now);
  EXPECT_LT(g.delayRateBps(), before);
  EXPECT_LE(g.delayRateBps(), 0.85 * 0.5e6 * 1.1);
  EXPECT_GE(g.delayRateBps(), 100e3);
  EXPECT_EQ(g.usage(), GccLite::Usage::Normal);  // one decrease per detection
  const double low = g.delayRateBps();
  // the queue drains: flat delay, the same 0.5 Mbps keeps being acknowledged
  for (int k = 1; k <= 30; ++k) {
    const Time t = now + k * fromMs(10);
    if (k % 2 == 0) g.onAck(AckInfo{t, t - fromMs(50), t - fromMs(20), kWireBytes, 0, fromMs(50), fromMs(30)});
    g.onTimer(t);
  }
  EXPECT_GT(g.delayRateBps(), low);  // back in Increase once the delay normalises
}

TEST(GccLite, MultiplicativeIncreaseFromStartRate) {
  GccLite g = make();
  EXPECT_DOUBLE_EQ(g.pacingRateBps(), 1e6);
  // feed acks matching the current rate so the incoming-rate clamp does not bind
  Time now = 0;
  for (int k = 1; k <= 100; ++k) {
    now = k * fromMs(10);
    const int pkts = static_cast<int>(g.pacingRateBps() * 0.01 / 8.0 / 1250.0) + 1;
    for (int p = 0; p < pkts; ++p) g.onAck(AckInfo{now, now - fromMs(40), now - fromMs(20), kWireBytes, 0, fromMs(40), fromMs(20)});
    g.onTimer(now);
  }
  EXPECT_GT(g.delayRateBps(), 2e6);  // grows while delay stays flat
}

TEST(GccLite, HighLossCutsTheLossBasedRate) {
  GccLite g = make();
  const double before = g.lossRateBps();
  Time now = 0;
  for (int k = 1; k <= 60; ++k) {
    now = k * fromMs(10);
    for (int i = 0; i < 8; ++i) g.onAck(AckInfo{now, now - fromMs(40), now - fromMs(20), kWireBytes, 0, fromMs(40), fromMs(20)});
    for (int i = 0; i < 2; ++i) g.onLoss(LossInfo{now, 0, kWireBytes, false});  // 20% loss
    g.onTimer(now);
  }
  EXPECT_LT(g.lossRateBps(), before);
  EXPECT_LE(g.pacingRateBps(), g.lossRateBps());
  EXPECT_GE(g.pacingRateBps(), 100e3);
}

TEST(GccLite, RandomInputNeverProducesNanOrInf) {
  GccLite g = make();
  std::mt19937_64 rng(11);
  Time now = 0;
  for (int i = 0; i < 100'000; ++i) {
    now += static_cast<Time>(rng() % 3'000'000);
    const Duration owd = static_cast<Duration>(rng() % 400'000'000);
    const Time send = now - owd;
    switch (rng() % 4) {
      case 0: g.onLoss(LossInfo{now, static_cast<std::uint64_t>(i), kWireBytes, rng() % 2 == 0}); break;
      case 1: g.onTimer(now); break;
      default: g.onAck(AckInfo{now, send, now - owd / 2, kWireBytes, static_cast<std::uint64_t>(i), owd, owd / 2});
    }
    const double r = g.pacingRateBps();
    ASSERT_TRUE(std::isfinite(r)) << i;
    ASSERT_GE(r, 100e3) << i;
    ASSERT_GT(g.inflightCapBytes(), 0u);
  }
  EXPECT_EQ(std::string(g.name()), "gcc_lite");
  EXPECT_GT(g.probe().chat_bps, 0.0);
}
