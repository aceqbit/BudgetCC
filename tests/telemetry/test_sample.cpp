#include <gtest/gtest.h>

#include <cstring>
#include <type_traits>

#include "telemetry/sample.hpp"

using namespace bcc;

TEST(Sample, TriviallyCopyableAndFixedSize) {
  static_assert(std::is_trivially_copyable_v<Sample>);
  static_assert(sizeof(Sample) == 80);
  Sample a;
  a.t_ms = 1230;
  a.q_ms = 42.0;
  Sample b;
  std::memcpy(&b, &a, sizeof a);
  EXPECT_EQ(b.t_ms, 1230);
  EXPECT_DOUBLE_EQ(b.q_ms, 42.0);
}

TEST(Sample, DefaultsAreZero) {
  Sample s;
  EXPECT_EQ(s.flow, 0);
  EXPECT_EQ(s.lost, 0);
  EXPECT_DOUBLE_EQ(s.chat_mbps, 0.0);
}
