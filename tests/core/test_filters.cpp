#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <random>
#include <vector>

#include "core/filters.hpp"

using namespace bcc;

TEST(Filters, WindowedMinMatchesBruteForce) {
  std::mt19937_64 rng(1);
  const Duration window = fromMs(100);
  WindowedMin<std::int64_t> mn(window);
  WindowedMax<std::int64_t> mx(window);
  std::vector<std::pair<Time, std::int64_t>> all;
  Time t = 0;
  for (int i = 0; i < 100'000; ++i) {
    t += static_cast<Time>(rng() % 5'000'000);  // 0-5 ms steps
    const std::int64_t v = static_cast<std::int64_t>(rng() % 1000);
    mn.update(t, v);
    mx.update(t, v);
    all.emplace_back(t, v);
    if (i % 50 == 0) {
      std::int64_t bmin = std::numeric_limits<std::int64_t>::max(), bmax = -1;
      for (auto it = all.rbegin(); it != all.rend() && it->first > t - window; ++it) {
        bmin = std::min(bmin, it->second);
        bmax = std::max(bmax, it->second);
      }
      ASSERT_EQ(mn.get(t), bmin);
      ASSERT_EQ(mx.get(t), bmax);
    }
  }
}

TEST(Filters, WindowExpiryBoundaryIsExclusiveAtOldEnd) {
  WindowedMin<int> mn(fromMs(10));
  mn.update(0, 1);
  mn.update(fromMs(5), 9);
  EXPECT_EQ(mn.get(fromMs(9)), 1);
  EXPECT_EQ(mn.get(fromMs(10)), 9);  // sample at t=0 is exactly window old: expired
  EXPECT_FALSE(mn.empty(fromMs(10)));
  EXPECT_TRUE(mn.empty(fromMs(15)));
}

TEST(Filters, RateWindowComputesBitsPerSecond) {
  RateWindow rw(fromSec(1));
  for (int i = 1; i <= 100; ++i) rw.add(fromMs(i), 1250);  // 1 pkt per ms
  // (80 ms, 100 ms] holds 20 packets: 20 * 1250 * 8 / 0.020 = 10 Mbps
  EXPECT_DOUBLE_EQ(rw.rateBps(fromMs(100), fromMs(20)), 10e6);
  EXPECT_DOUBLE_EQ(rw.rateBps(fromMs(100), fromMs(100)), 10e6);
  EXPECT_DOUBLE_EQ(rw.rateBps(fromMs(500), fromMs(20)), 0.0);
}

TEST(Filters, RateWindowRandomMatchesBruteForce) {
  std::mt19937_64 rng(5);
  RateWindow rw(fromMs(500));
  std::vector<std::pair<Time, std::uint32_t>> all;
  Time t = 0;
  for (int i = 0; i < 100'000; ++i) {
    t += static_cast<Time>(rng() % 3'000'000);
    const std::uint32_t b = 1 + static_cast<std::uint32_t>(rng() % 1500);
    rw.add(t, b);
    all.emplace_back(t, b);
    if (i % 100 == 0) {
      const Duration w = fromMs(20 + static_cast<int>(rng() % 400));
      std::uint64_t s = 0;
      for (auto it = all.rbegin(); it != all.rend() && it->first > t - w; ++it) s += it->second;
      ASSERT_DOUBLE_EQ(rw.rateBps(t, w), static_cast<double>(s) * 8.0 / toSec(w));
    }
  }
}

TEST(Filters, WarmRateDividesByDataAgeWhileTheWindowFills) {
  RateWindow rw(fromSec(1));
  EXPECT_DOUBLE_EQ(rw.rateBpsWarm(fromMs(5), fromMs(250)), 0.0);  // no samples yet
  for (int i = 0; i < 50; ++i) rw.add(fromMs(100 + i), 1250);      // 1 pkt/ms from t = 100 ms
  // at t = 150 ms the data is 50 ms old: 10 Mbps, not 50 * 1250 * 8 / 0.25 s = 2 Mbps
  EXPECT_NEAR(rw.rateBpsWarm(fromMs(149), fromMs(250)), 10e6, 0.3e6);
  EXPECT_NEAR(rw.rateBps(fromMs(149), fromMs(250)), 2e6, 0.1e6);
  // right after the first sample the divisor is floored at 20 ms
  RateWindow fresh(fromSec(1));
  fresh.add(fromMs(10), 1250);
  EXPECT_DOUBLE_EQ(fresh.rateBpsWarm(fromMs(10), fromMs(250)), 1250.0 * 8.0 / 0.02);
  // once the window is full both agree
  for (int i = 50; i < 400; ++i) rw.add(fromMs(100 + i), 1250);
  EXPECT_DOUBLE_EQ(rw.rateBpsWarm(fromMs(499), fromMs(250)), rw.rateBps(fromMs(499), fromMs(250)));
}
