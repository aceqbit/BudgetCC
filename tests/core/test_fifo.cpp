#include <gtest/gtest.h>

#include <deque>
#include <random>

#include "core/fifo.hpp"

using namespace bcc;

TEST(Fifo, OrderWrapAroundAndGrowth) {
  Fifo<int> f(4);
  EXPECT_TRUE(f.empty());
  EXPECT_EQ(f.capacity(), 4u);
  for (int i = 0; i < 3; ++i) f.push(i);
  f.pop();
  f.pop();
  for (int i = 3; i < 6; ++i) f.push(i);  // wraps
  EXPECT_EQ(f.capacity(), 4u);
  f.push(6);  // forces growth while wrapped
  EXPECT_EQ(f.capacity(), 8u);
  for (int i = 2; i <= 6; ++i) {
    ASSERT_EQ(f.front(), i);
    f.pop();
  }
  EXPECT_TRUE(f.empty());
}

TEST(Fifo, DifferentialAgainstDeque) {
  Fifo<std::uint64_t> f(2);
  std::deque<std::uint64_t> ref;
  std::mt19937_64 rng(5);
  for (int i = 0; i < 200000; ++i) {
    if (ref.empty() || rng() % 5 < 3) {
      const std::uint64_t v = rng();
      f.push(v);
      ref.push_back(v);
    } else {
      ASSERT_EQ(f.front(), ref.front());
      f.pop();
      ref.pop_front();
    }
    ASSERT_EQ(f.size(), ref.size());
  }
}
