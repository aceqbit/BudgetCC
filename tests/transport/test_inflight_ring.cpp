#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <vector>

#include "reference/map_inflight.hpp"
#include "transport/inflight_ring.hpp"

using namespace bcc;

TEST(InflightRing, InsertFindEraseAndOldest) {
  InflightRing r(8);
  EXPECT_TRUE(r.empty());
  r.insert(10, Sent{100, 1250, 0});
  r.insert(12, Sent{120, 1250, 0});
  EXPECT_EQ(r.size(), 2u);
  EXPECT_EQ(r.oldestSeq(), 10u);
  EXPECT_EQ(r.oldest().send_ts, 100);
  EXPECT_EQ(r.find(11), nullptr);
  ASSERT_NE(r.find(12), nullptr);
  r.erase(10);
  EXPECT_EQ(r.oldestSeq(), 12u);  // oldest skips the hole
  r.erase(11);                    // not present: no-op
  r.erase(12);
  EXPECT_TRUE(r.empty());
  EXPECT_EQ(r.find(12), nullptr);
}

TEST(InflightRing, GrowsWhenSpanExceedsCapacityAndKeepsEntries) {
  InflightRing r(4);
  for (std::uint64_t s = 0; s < 4; ++s) r.insert(s, Sent{static_cast<Time>(s), 1, 0});
  r.insert(9, Sent{9, 1, 0});  // span 10 > 4: doubles twice
  EXPECT_GE(r.capacity(), 16u);
  for (std::uint64_t s = 0; s < 4; ++s) {
    ASSERT_NE(r.find(s), nullptr);
    EXPECT_EQ(r.find(s)->send_ts, static_cast<Time>(s));
  }
  ASSERT_NE(r.find(9), nullptr);
}

TEST(InflightRing, RejectsBadInsert) {
  InflightRing r(8);
  r.insert(5, Sent{});
  EXPECT_THROW(r.insert(4, Sent{}), std::logic_error);
  EXPECT_THROW(r.insert(5, Sent{}), std::logic_error);
}

TEST(InflightRing, ForEachBelowVisitsAscendingAndAllowsErase) {
  InflightRing r(8);
  for (std::uint64_t s = 0; s < 6; ++s) r.insert(s, Sent{});
  r.erase(2);
  std::vector<std::uint64_t> seen;
  r.forEachBelow(5, [&](std::uint64_t s, Sent&) {
    seen.push_back(s);
    r.erase(s);
  });
  EXPECT_EQ(seen, (std::vector<std::uint64_t>{0, 1, 3, 4}));
  EXPECT_EQ(r.size(), 1u);
  EXPECT_EQ(r.oldestSeq(), 5u);
}

TEST(InflightRing, DifferentialAgainstStdMap) {
  InflightRing ring(4);
  bccref::MapInflight ref;
  std::mt19937_64 rng(77);
  std::uint64_t next = 0;
  for (int i = 0; i < 200000; ++i) {
    const auto op = rng() % 10;
    if (op < 4) {
      const Sent s{static_cast<Time>(rng() % 1000), static_cast<std::uint32_t>(rng() % 1500), 0};
      ring.insert(next, s);
      ref.insert(next, s);
      next += 1 + rng() % 2;
    } else if (op < 7 && !ref.empty()) {
      const std::uint64_t seq = ref.oldestSeq() + rng() % 40;
      ring.erase(seq);
      ref.erase(seq);
    } else if (op < 9 && !ref.empty()) {
      const std::uint64_t lim = ref.oldestSeq() + rng() % 40;
      std::vector<std::uint64_t> a, b;
      ring.forEachBelow(lim, [&](std::uint64_t s, Sent& v) {
        ++v.later_acked;
        a.push_back(s);
      });
      ref.forEachBelow(lim, [&](std::uint64_t s, bcc::Sent& v) {
        ++v.later_acked;
        b.push_back(s);
      });
      ASSERT_EQ(a, b);
    }
    ASSERT_EQ(ring.size(), ref.size());
    if (!ref.empty()) {
      ASSERT_EQ(ring.oldestSeq(), ref.oldestSeq());
      ASSERT_EQ(ring.oldest().later_acked, ref.oldest().later_acked);
    }
    const std::uint64_t probe = rng() % (next + 1);
    const Sent* x = ring.find(probe);
    const Sent* y = ref.find(probe);
    ASSERT_EQ(x == nullptr, y == nullptr);
    if (x != nullptr) {
      ASSERT_EQ(x->send_ts, y->send_ts);
      ASSERT_EQ(x->later_acked, y->later_acked);
    }
  }
}
