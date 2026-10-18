#include <gtest/gtest.h>

#include <cstdint>
#include <thread>

#include "core/ring_buffer.hpp"

using namespace bcc;

template <class R>
struct RingTraits;
template <>
struct RingTraits<MutexRing<std::uint64_t>> {
  static constexpr std::uint64_t kStressItems = 1'000'000;
};

template <class R>
class RingTest : public ::testing::Test {};
using RingTypes = ::testing::Types<MutexRing<std::uint64_t>>;
TYPED_TEST_SUITE(RingTest, RingTypes, ::testing::internal::DefaultNameGenerator);

TYPED_TEST(RingTest, FifoWrapAroundFullEmpty) {
  TypeParam r(4);
  std::uint64_t v = 0;
  EXPECT_FALSE(r.pop(v));
  for (std::uint64_t i = 0; i < 4; ++i) EXPECT_TRUE(r.push(i));
  EXPECT_FALSE(r.push(99));
  EXPECT_EQ(r.dropped(), 1u);
  EXPECT_EQ(r.size(), 4u);
  for (std::uint64_t i = 0; i < 4; ++i) {
    ASSERT_TRUE(r.pop(v));
    EXPECT_EQ(v, i);
  }
  // wrap-around several times
  for (std::uint64_t round = 0; round < 10; ++round) {
    for (std::uint64_t i = 0; i < 3; ++i) ASSERT_TRUE(r.push(round * 10 + i));
    for (std::uint64_t i = 0; i < 3; ++i) {
      ASSERT_TRUE(r.pop(v));
      ASSERT_EQ(v, round * 10 + i);
    }
  }
  EXPECT_EQ(r.size(), 0u);
  EXPECT_EQ(r.capacity(), 4u);
  EXPECT_EQ(r.dropped(), 1u);
}

TYPED_TEST(RingTest, OverflowNeverBlocksAndCountsDrops) {
  TypeParam r(8);
  for (std::uint64_t i = 0; i < 100; ++i) r.push(i);
  EXPECT_EQ(r.size(), 8u);
  EXPECT_EQ(r.dropped(), 92u);
}

TYPED_TEST(RingTest, TwoThreadStressPreservesOrder) {
  constexpr std::uint64_t kN = RingTraits<TypeParam>::kStressItems;
  TypeParam r(1024);
  std::thread prod([&] {
    for (std::uint64_t i = 0; i < kN; ++i)
      while (!r.push(i)) std::this_thread::yield();
  });
  std::uint64_t expect = 0;
  std::uint64_t v = 0;
  while (expect < kN) {
    if (r.pop(v)) {
      ASSERT_EQ(v, expect);
      ++expect;
    } else {
      std::this_thread::yield();
    }
  }
  prod.join();
}
