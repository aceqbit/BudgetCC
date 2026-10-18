#include <gtest/gtest.h>

#include <array>
#include <random>

#include "transport/rtp_header.hpp"

using namespace bcc;

TEST(RtpHeader, SizeAndGoldenBytes) {
  static_assert(kRtpHeaderSize == 12);
  RtpHeader h;
  h.marker = true;
  h.payload_type = 96;
  h.seq = 0x1234;
  h.timestamp = 0xdeadbeef;
  h.ssrc = 0x01020304;
  std::array<std::uint8_t, 12> b{};
  encodeRtp(h, b.data());
  const std::array<std::uint8_t, 12> golden = {0x80, 0xe0, 0x12, 0x34, 0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04};
  EXPECT_EQ(b, golden);
}

TEST(RtpHeader, RoundTripRandom) {
  std::mt19937_64 rng(3);
  for (int i = 0; i < 100'000; ++i) {
    RtpHeader h;
    h.marker = rng() & 1U;
    h.payload_type = static_cast<std::uint8_t>(rng() & 0x7fU);
    h.seq = static_cast<std::uint16_t>(rng());
    h.timestamp = static_cast<std::uint32_t>(rng());
    h.ssrc = static_cast<std::uint32_t>(rng());
    std::array<std::uint8_t, 12> b{};
    encodeRtp(h, b.data());
    RtpHeader d;
    ASSERT_TRUE(decodeRtp(b.data(), b.size(), d));
    ASSERT_TRUE(h == d);
  }
}

TEST(RtpHeader, RejectsShortBufferAndBadVersion) {
  std::array<std::uint8_t, 12> b{};
  RtpHeader h, d;
  encodeRtp(h, b.data());
  EXPECT_FALSE(decodeRtp(b.data(), 11, d));
  EXPECT_FALSE(decodeRtp(b.data(), 0, d));
  b[0] = 0x40;  // version 1
  EXPECT_FALSE(decodeRtp(b.data(), 12, d));
}

TEST(SeqUnwrapper, WrapsAcross65535) {
  SeqUnwrapper u;
  EXPECT_EQ(u.unwrap(65530), 65530u);
  EXPECT_EQ(u.unwrap(65535), 65535u);
  EXPECT_EQ(u.unwrap(0), 65536u);
  EXPECT_EQ(u.unwrap(1), 65537u);
  EXPECT_EQ(u.unwrap(100), 65636u);
}

TEST(SeqUnwrapper, ToleratesSmallReorderingAcrossWrap) {
  SeqUnwrapper u;
  u.unwrap(65534);
  u.unwrap(1);                       // 65537
  EXPECT_EQ(u.unwrap(65535), 65535u);  // late packet from before the wrap
  EXPECT_EQ(u.unwrap(2), 65538u);
}

TEST(SeqUnwrapper, LongMonotoneStreamMatchesTruth) {
  SeqUnwrapper u;
  for (std::uint64_t s = 0; s < 300'000; ++s) ASSERT_EQ(u.unwrap(static_cast<std::uint16_t>(s)), s);
}
