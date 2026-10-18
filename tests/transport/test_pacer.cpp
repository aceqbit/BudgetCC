#include <gtest/gtest.h>

#include "transport/pacer.hpp"

using namespace bcc;

TEST(Pacer, GapEqualsSizeOverRate) {
  Pacer p(10e6);  // 1250 B at 10 Mbps = 1 ms
  EXPECT_TRUE(p.ready(0));
  p.onSend(0, 1250);
  EXPECT_EQ(p.nextSend(), fromMs(1));
  EXPECT_FALSE(p.ready(fromMs(1) - 1));
  EXPECT_TRUE(p.ready(fromMs(1)));
}

TEST(Pacer, NoDriftOver100kPackets) {
  Pacer p(7e6 + 123.0);  // gap is not an integer number of ns
  Time t = 0;
  for (int i = 0; i < 100'000; ++i) {
    ASSERT_TRUE(p.ready(t));
    p.onSend(t, 1250);
    t = p.nextSend();
  }
  const double exact = 100'000.0 * 1250.0 * 8e9 / (7e6 + 123.0);
  EXPECT_NEAR(static_cast<double>(t), exact, 1.0);
}

TEST(Pacer, IdleTimeIsNotBanked) {
  Pacer p(10e6);
  p.onSend(0, 1250);
  p.onSend(fromSec(1), 1250);  // long idle gap, then a send
  EXPECT_EQ(p.nextSend(), fromSec(1) + fromMs(1));
}

TEST(Pacer, SetRateReschedulesNextSend) {
  Pacer p(10e6);
  p.onSend(0, 1250);
  EXPECT_EQ(p.nextSend(), fromMs(1));
  p.setRate(5e6);
  EXPECT_EQ(p.nextSend(), fromMs(2));
  p.setRate(20e6);
  EXPECT_EQ(p.nextSend(), fromUs(500));
  EXPECT_DOUBLE_EQ(p.rateBps(), 20e6);
}

TEST(Pacer, RateFloorAvoidsDivisionByZero) {
  Pacer p(0.0);
  EXPECT_DOUBLE_EQ(p.rateBps(), Pacer::kMinRateBps);
  p.onSend(0, 1250);
  p.setRate(-5.0);
  EXPECT_GT(p.nextSend(), fromSec(1));
}
