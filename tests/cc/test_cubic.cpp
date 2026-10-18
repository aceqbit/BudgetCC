#include <gtest/gtest.h>

#include <cmath>

#include "cc/cubic.hpp"

using namespace bcc;

namespace {
Cubic make() {
  static const std::map<std::string, double> none;
  ParamReader p(none, "cubic");
  return Cubic(p);
}
AckInfo ack(Time now, Duration rtt) { return AckInfo{now, now - rtt, now - rtt / 2, kWireBytes, 0, rtt, rtt / 2}; }
}  // namespace

TEST(Cubic, ClosedFormMatchesRfc8312) {
  const double w_max = 100.0;
  const double k = std::cbrt(w_max * (1.0 - 0.7) / 0.4);
  EXPECT_DOUBLE_EQ(Cubic::cubicK(w_max), k);
  EXPECT_NEAR(Cubic::cubicWindow(0.0, w_max), 0.7 * w_max, 1e-9);  // right after a loss: beta * Wmax
  EXPECT_NEAR(Cubic::cubicWindow(k, w_max), w_max, 1e-9);          // plateau at Wmax
  EXPECT_NEAR(Cubic::cubicWindow(k + 2.0, w_max), 0.4 * 8.0 + w_max, 1e-9);
  EXPECT_NEAR(Cubic::cubicWindow(k - 1.0, w_max), -0.4 + w_max, 1e-9);
}

TEST(Cubic, LossReducesByBetaAndRemembersWmax) {
  Cubic c = make();
  for (int i = 0; i < 90; ++i) c.onAck(ack(fromMs(40), fromMs(40)));
  ASSERT_DOUBLE_EQ(c.cwndBytes(), 100.0 * kWireBytes);
  c.onLoss(LossInfo{fromMs(100), 0, kWireBytes, false});
  EXPECT_NEAR(c.cwndBytes() / kWireBytes, 70.0, 1e-9);
  EXPECT_NEAR(c.wMaxPkts(), 100.0, 1e-9);
  // a second, lower loss triggers fast convergence: Wmax = cwnd * (1 + beta) / 2
  c.onLoss(LossInfo{fromMs(1000), 0, kWireBytes, false});
  EXPECT_NEAR(c.cwndBytes() / kWireBytes, 49.0, 1e-9);
  EXPECT_NEAR(c.wMaxPkts(), 70.0 * 1.7 / 2.0, 1e-9);
}

TEST(Cubic, WindowTracksClosedFormWithinOnePacket) {
  // After a loss at Wmax = 100 pkts, feed ACKs clocked by the window (rtt 50 ms) and compare the
  // window against max(W_cubic(t + rtt), W_est(t)), the RFC's target.
  Cubic c = make();
  const Duration rtt = fromMs(50);
  for (int i = 0; i < 90; ++i) c.onAck(ack(fromMs(40), rtt));
  const Time t_loss = fromMs(100);
  c.onLoss(LossInfo{t_loss, 0, kWireBytes, false});
  const double w_max = 100.0;
  double owed = 0.0;
  double worst = 0.0;
  for (Time t = t_loss + fromMs(1); t < t_loss + fromSec(8); t += fromMs(1)) {
    owed += c.cwndBytes() / kWireBytes * (1.0 / 50.0);  // acks due in this millisecond
    while (owed >= 1.0) {
      c.onAck(ack(t, rtt));
      owed -= 1.0;
    }
    const double ts = toSec(t - t_loss);
    const double w_est = 0.7 * w_max + 3.0 * 0.3 / 1.7 * ts / 0.05;
    const double target = std::max(Cubic::cubicWindow(ts + 0.05, w_max), w_est);
    if (ts > 0.5) worst = std::max(worst, std::fabs(c.cwndBytes() / kWireBytes - target));
  }
  EXPECT_LE(worst, 1.0);
}

TEST(Cubic, TimeoutCollapsesToTwoPacketsAndSlowStartResumes) {
  Cubic c = make();
  for (int i = 0; i < 90; ++i) c.onAck(ack(fromMs(40), fromMs(40)));
  c.onLoss(LossInfo{fromMs(100), 0, kWireBytes, true});
  EXPECT_DOUBLE_EQ(c.cwndBytes(), 2.0 * kWireBytes);
  const double before = c.cwndBytes();
  c.onAck(ack(fromMs(200), fromMs(40)));
  EXPECT_DOUBLE_EQ(c.cwndBytes(), before + kWireBytes);  // still below ssthresh: slow start
  EXPECT_EQ(std::string(c.name()), "cubic");
}
