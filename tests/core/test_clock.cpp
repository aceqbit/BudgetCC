#include <gtest/gtest.h>

#include <limits>
#include <utility>

#include "core/clock.hpp"

using namespace bcc;

namespace {
template <class T, class = void>
struct AcceptsMs : std::false_type {};
template <class T>
struct AcceptsMs<T, std::void_t<decltype(fromMs(std::declval<T>()))>> : std::true_type {};
}  // namespace

TEST(Clock, ConversionsAreExact) {
  EXPECT_EQ(fromNs(7), 7);
  EXPECT_EQ(fromUs(3), 3'000);
  EXPECT_EQ(fromMs(20), 20'000'000);
  EXPECT_EQ(fromSec(60), 60'000'000'000LL);
  EXPECT_DOUBLE_EQ(toMs(fromMs(150)), 150.0);
  EXPECT_DOUBLE_EQ(toSec(fromSec(2)), 2.0);
  EXPECT_DOUBLE_EQ(toUs(fromUs(5)), 5.0);
}

TEST(Clock, SixtySecondsFitsInt64WithMargin) {
  const Time t = fromSec(60);
  EXPECT_LT(t, std::numeric_limits<Time>::max() / 1'000'000);
}

TEST(Clock, NoImplicitDoubleConversion) {
  static_assert(AcceptsMs<int>::value);
  static_assert(AcceptsMs<long long>::value);
  static_assert(!AcceptsMs<double>::value);
  static_assert(!AcceptsMs<float>::value);
  SUCCEED();
}
