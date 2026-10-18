#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <thread>
#include <type_traits>

#include "core/ring_buffer.hpp"
#include "reference/mutex_ring.hpp"

using namespace bcc;

template <class R>
struct RingTraits;
template <>
struct RingTraits<bccref::MutexRing<std::uint64_t>> {
  static constexpr std::uint64_t kStressItems = 1'000'000;
};
// 1e7 items in a normal build; sanitizer builds run 1e6 to stay inside their time limit.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
constexpr std::uint64_t kSpscStress = 1'000'000;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
constexpr std::uint64_t kSpscStress = 1'000'000;
#else
constexpr std::uint64_t kSpscStress = 10'000'000;
#endif
#else
constexpr std::uint64_t kSpscStress = 10'000'000;
#endif
template <>
struct RingTraits<SpscRing<std::uint64_t>> {
  static constexpr std::uint64_t kStressItems = kSpscStress;
};

template <class R>
class RingTest : public ::testing::Test {};
using RingTypes = ::testing::Types<bccref::MutexRing<std::uint64_t>, SpscRing<std::uint64_t>>;
struct RingNames {
  template <class R>
  static std::string GetName(int) {
    return std::is_same<R, SpscRing<std::uint64_t>>::value ? "Spsc" : "Mutex";
  }
};
TYPED_TEST_SUITE(RingTest, RingTypes, RingNames);

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

TEST(SpscRing, DifferentialAgainstMutexRingOnRandomOperations) {
  SpscRing<std::uint64_t> a(7);
  bccref::MutexRing<std::uint64_t> b(7);
  std::uint64_t state = 99;
  for (int i = 0; i < 200000; ++i) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    if ((state >> 33) % 5 < 3) {
      ASSERT_EQ(a.push(state), b.push(state));
    } else {
      std::uint64_t x = 0, y = 0;
      const bool pa = a.pop(x), pb = b.pop(y);
      ASSERT_EQ(pa, pb);
      if (pa) {
        ASSERT_EQ(x, y);
      }
    }
    ASSERT_EQ(a.size(), b.size());
    ASSERT_EQ(a.dropped(), b.dropped());
  }
}
