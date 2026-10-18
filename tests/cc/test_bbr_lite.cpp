#include <gtest/gtest.h>

#include <set>

#include "cc/bbr_lite.hpp"
#include "harness/runner.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

namespace {
BbrLite make() {
  static const std::map<std::string, double> none;
  ParamReader p(none, "bbr_lite");
  return BbrLite(p);
}
AckInfo ack(Time now, Duration rtt, std::uint32_t bytes = kWireBytes) {
  return AckInfo{now, now - rtt, now - rtt / 2, bytes, 0, rtt, rtt / 2};
}
const BbrLite& bbrOf(Experiment& e) { return dynamic_cast<const BbrLite&>(e.sender(0).cc()); }
}  // namespace

TEST(BbrLite, StartupExitsAfterThreeRoundsWithoutQuarterGrowth) {
  BbrLite b = make();
  const Duration rtt = fromMs(40);
  Time left_startup = 0;
  for (int ms = 1; ms <= 400; ++ms) {  // a constant 10 Mbps delivery rate: one 1250 B packet per ms
    b.onAck(ack(fromMs(ms), rtt));
    b.onTimer(fromMs(ms));
    if (left_startup == 0 && b.mode() != BbrLite::Mode::Startup) left_startup = fromMs(ms);
  }
  // round ends at 40, 80, 120, 160 ms: the first sets the reference, the next three show no growth
  EXPECT_EQ(left_startup, fromMs(160));
  EXPECT_NEAR(b.btlBwBps(), 10e6, 0.1e6);
  EXPECT_NE(b.mode(), BbrLite::Mode::Startup);
}

TEST(BbrLite, GrowthKeepsStartupGoing) {
  BbrLite b = make();
  const Duration rtt = fromMs(40);
  Time now = 0;
  // delivery rate doubles every 40 ms round for 6 rounds
  for (int round = 0; round < 6; ++round) {
    const int pkts_per_round = 4 << round;
    for (int i = 0; i < 40; ++i) {
      now += fromMs(1);
      for (int k = 0; k < pkts_per_round / 40 + (i < pkts_per_round % 40 ? 1 : 0); ++k) b.onAck(ack(now, rtt));
    }
    EXPECT_EQ(b.mode(), BbrLite::Mode::Startup) << "round " << round;
  }
}

TEST(BbrLite, PacingAndCapFollowBandwidthDelayProduct) {
  BbrLite b = make();
  EXPECT_DOUBLE_EQ(b.pacingRateBps(), 2.885 * 10 * 1250 * 8 / 0.1);  // before any sample
  EXPECT_EQ(b.inflightCapBytes(), 10u * kWireBytes);
  const Duration rtt = fromMs(40);
  for (int ms = 1; ms <= 1000; ++ms) {
    b.onAck(ack(fromMs(ms), rtt));
    b.onTimer(fromMs(ms));
  }
  ASSERT_EQ(b.mode(), BbrLite::Mode::ProbeBw);
  const double bdp = b.btlBwBps() / 8.0 * 0.04;
  EXPECT_NEAR(static_cast<double>(b.inflightCapBytes()), 2.0 * bdp, 0.1 * 2.0 * bdp);
  EXPECT_NEAR(b.pacingRateBps(), b.pacingGain() * b.btlBwBps(), 1.0);
  EXPECT_EQ(b.minRtt(), rtt);
  EXPECT_GT(b.probe().chat_bps, 0.0);
  EXPECT_EQ(std::string(b.name()), "bbr_lite");
}

TEST(BbrLite, ProbeBwCyclesThroughEightGainPhases) {
  BbrLite b = make();
  const Duration rtt = fromMs(40);
  std::set<double> gains;
  for (int ms = 1; ms <= 2000; ++ms) {
    b.onAck(ack(fromMs(ms), rtt));
    b.onTimer(fromMs(ms));
    if (b.mode() == BbrLite::Mode::ProbeBw) gains.insert(b.pacingGain());
  }
  EXPECT_EQ(gains, (std::set<double>{0.75, 1.0, 1.25}));
}

TEST(BbrLite, FindsLinkRateOnConstantLinkAndHoldsTwoBdp) {
  static const Trace t = Trace::parse(bcc_test::constantTrace(12.0, 60000), "c12");
  RunConfig c;
  c.algos = {"bbr_lite"};
  c.buffer_pkts = 1000;
  c.duration = fromSec(20);
  Experiment e(c, t);
  e.runUntil(c.duration);
  const BbrLite& b = bbrOf(e);
  EXPECT_NEAR(b.btlBwBps(), 12e6, 1.2e6);  // within 10% of the link rate
  EXPECT_EQ(b.mode(), BbrLite::Mode::ProbeBw);
  const double bdp = b.btlBwBps() / 8.0 * toSec(b.minRtt());
  EXPECT_NEAR(static_cast<double>(b.inflightCapBytes()), 2.0 * bdp, 0.1 * 2.0 * bdp);
  EXPECT_GE(e.finish().summary.utilisation_pct, 90.0);
}

TEST(BbrLite, ReactsToCapacityDrop) {
  static const Trace t = Trace::parse(bcc_test::rateTrace({{0, 12.0}, {10000, 3.0}}, 60000), "step");
  RunConfig c;
  c.algos = {"bbr_lite"};
  c.buffer_pkts = 100;
  c.duration = fromSec(40);
  Experiment e(c, t);
  e.runUntil(fromSec(9));
  EXPECT_GT(bbrOf(e).btlBwBps(), 10e6);
  e.runUntil(c.duration);
  EXPECT_LT(bbrOf(e).btlBwBps(), 3.6e6);  // the 10-round max filter has aged out the old rate
  EXPECT_GT(bbrOf(e).btlBwBps(), 2.4e6);
}

TEST(BbrLite, LossIsIgnored) {
  BbrLite b = make();
  for (int ms = 1; ms <= 500; ++ms) b.onAck(ack(fromMs(ms), fromMs(40)));
  const double rate = b.pacingRateBps();
  const auto cap = b.inflightCapBytes();
  b.onLoss(LossInfo{fromMs(500), 1, kWireBytes, true});
  EXPECT_DOUBLE_EQ(b.pacingRateBps(), rate);
  EXPECT_EQ(b.inflightCapBytes(), cap);
}
