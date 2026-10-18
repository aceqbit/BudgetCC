#include <gtest/gtest.h>

#include "cc/reno.hpp"

using namespace bcc;

namespace {
AckInfo ack(Time now, Duration rtt = fromMs(40)) { return AckInfo{now, now - rtt, now - rtt / 2, kWireBytes, 0, rtt, rtt / 2}; }
LossInfo loss(Time now, bool timeout = false) { return LossInfo{now, 0, kWireBytes, timeout}; }

Reno make() {
  static const std::map<std::string, double> none;
  ParamReader p(none, "reno");
  return Reno(p);
}
}  // namespace

TEST(Reno, InitialWindowAndDefaultRttPacing) {
  Reno r = make();
  EXPECT_DOUBLE_EQ(r.cwndBytes(), 10.0 * kWireBytes);
  EXPECT_EQ(r.inflightCapBytes(), 10u * kWireBytes);
  // slow start: gain 2.0, srtt defaults to 100 ms: 2 * 12500 B * 8 / 0.1 s = 2 Mbps
  EXPECT_DOUBLE_EQ(r.pacingRateBps(), 2e6);
}

TEST(Reno, SlowStartDoublesPerRtt) {
  Reno r = make();
  for (int i = 0; i < 10; ++i) r.onAck(ack(fromMs(40 + i)));
  EXPECT_DOUBLE_EQ(r.cwndBytes(), 20.0 * kWireBytes);
  for (int i = 0; i < 20; ++i) r.onAck(ack(fromMs(80 + i)));
  EXPECT_DOUBLE_EQ(r.cwndBytes(), 40.0 * kWireBytes);
}

TEST(Reno, CongestionAvoidanceAddsOnePacketPerRtt) {
  Reno r = make();
  for (int i = 0; i < 90; ++i) r.onAck(ack(fromMs(40)));  // cwnd -> 100 pkts
  ASSERT_DOUBLE_EQ(r.cwndBytes(), 100.0 * kWireBytes);
  r.onLoss(loss(fromMs(100)));
  EXPECT_DOUBLE_EQ(r.cwndBytes(), 50.0 * kWireBytes);
  EXPECT_DOUBLE_EQ(r.ssthreshBytes(), 50.0 * kWireBytes);
  const double before = r.cwndBytes() / kWireBytes;
  for (int i = 0; i < 50; ++i) r.onAck(ack(fromMs(200 + i)));  // one window of acks
  EXPECT_NEAR(r.cwndBytes() / kWireBytes - before, 1.0, 0.05);
  // congestion avoidance pacing uses gain 1.25 and the measured srtt (40 ms)
  EXPECT_NEAR(r.pacingRateBps(), 1.25 * r.cwndBytes() * 8.0 / 0.04, 1.0);
}

TEST(Reno, HalvesOncePerRttWindow) {
  Reno r = make();
  for (int i = 0; i < 90; ++i) r.onAck(ack(fromMs(40)));
  r.onLoss(loss(fromMs(100)));
  const double after_first = r.cwndBytes();
  r.onLoss(loss(fromMs(110)));  // same congestion event (< srtt = 40 ms later)
  r.onLoss(loss(fromMs(130)));
  EXPECT_DOUBLE_EQ(r.cwndBytes(), after_first);
  r.onLoss(loss(fromMs(141)));  // a new event
  EXPECT_DOUBLE_EQ(r.cwndBytes(), after_first / 2.0);
}

TEST(Reno, TimeoutCollapsesToTwoPackets) {
  Reno r = make();
  for (int i = 0; i < 90; ++i) r.onAck(ack(fromMs(40)));
  r.onLoss(loss(fromMs(100), true));
  EXPECT_DOUBLE_EQ(r.cwndBytes(), 2.0 * kWireBytes);
  EXPECT_DOUBLE_EQ(r.ssthreshBytes(), 50.0 * kWireBytes);
}

TEST(Reno, WindowNeverBelowTwoPackets) {
  Reno r = make();
  for (int i = 0; i < 20; ++i) r.onLoss(loss(fromMs(1000 * (i + 1))));
  EXPECT_DOUBLE_EQ(r.cwndBytes(), 2.0 * kWireBytes);
  EXPECT_EQ(std::string(r.name()), "reno");
  EXPECT_DOUBLE_EQ(r.probe().cwnd_bytes, r.cwndBytes());
}
