#include <gtest/gtest.h>

#include <cmath>

#include "transport/traffic_source.hpp"

using namespace bcc;

TEST(TrafficSource, BulkIsAlwaysBacklogged) {
  BulkSource s;
  for (int i = 0; i < 1000; ++i) {
    ASSERT_TRUE(s.hasPacket(fromMs(i)));
    s.consume();
  }
}

TEST(TrafficSource, VideoIsExactly30FpsOverASecond) {
  EXPECT_EQ(VideoSource::frameTime(0), 0);
  EXPECT_EQ(VideoSource::frameTime(30), fromSec(1));
  EXPECT_EQ(VideoSource::frameTime(1800), fromSec(60));
  for (std::uint64_t k = 1; k < 3000; ++k) {
    const Duration gap = VideoSource::frameTime(k) - VideoSource::frameTime(k - 1);
    ASSERT_GE(gap, 33'333'333);
    ASSERT_LE(gap, 33'333'334);
  }
}

TEST(TrafficSource, KeyFrameIsFourTimesDeltaEverySixtiethFrame) {
  VideoSource v;
  const double rate = 6e6;
  double key_bytes = 0, delta_bytes = 0, total = 0;
  for (std::uint64_t k = 0; k < 120; ++k) {
    v.generateFrame(VideoSource::frameTime(k), rate);
    total += v.lastFrameBytes();
    if (k % 60 == 0) {
      key_bytes = v.lastFrameBytes();
    } else {
      delta_bytes = v.lastFrameBytes();
    }
  }
  EXPECT_NEAR(key_bytes / delta_bytes, 4.0, 0.1);
  EXPECT_NEAR(total / 120.0, rate / 30.0 / 8.0, 1.0);  // average frame size = rate / 30
  EXPECT_EQ(v.counters().frames_generated, 120u);
}

TEST(TrafficSource, FramePacketisedIntoPayloadSizedPackets) {
  VideoSource v;
  v.generateFrame(0, 8e6);  // key frame
  const std::uint64_t expect = v.counters().packets_generated;
  EXPECT_EQ(expect, static_cast<std::uint64_t>(std::ceil(v.lastFrameBytes() / 1200.0)));
  std::uint64_t n = 0;
  while (v.hasPacket(0)) {
    v.consume();
    ++n;
  }
  EXPECT_EQ(n, expect);
  EXPECT_FALSE(v.hasPacket(0));
}

TEST(TrafficSource, StaleFrameDroppedAfterOneSecond) {
  VideoSource v;
  v.generateFrame(0, 1e6);
  v.generateFrame(fromMs(900), 1e6);
  EXPECT_TRUE(v.hasPacket(fromMs(1000)));  // exactly 1 s old is not yet stale
  EXPECT_EQ(v.counters().frames_dropped, 0u);
  EXPECT_TRUE(v.hasPacket(fromMs(1001)));  // first frame dropped, second remains
  EXPECT_EQ(v.counters().frames_dropped, 1u);
  EXPECT_FALSE(v.hasPacket(fromMs(2000)));
  EXPECT_EQ(v.counters().frames_dropped, 2u);
}
