#include <gtest/gtest.h>

#include <vector>

#include "core/packet_pool.hpp"

using namespace bcc;

TEST(PacketPool, AllocFreeBalanceAndPeak) {
  PacketPool pool;
  std::vector<Packet*> v;
  for (int i = 0; i < 10; ++i) v.push_back(pool.alloc());
  EXPECT_EQ(pool.counters().allocs, 10u);
  EXPECT_EQ(pool.counters().in_use, 10u);
  EXPECT_EQ(pool.counters().peak_in_use, 10u);
  for (int i = 0; i < 10; i += 2) pool.free(v[static_cast<std::size_t>(i)]);
  EXPECT_EQ(pool.counters().in_use, 5u);
  EXPECT_EQ(pool.counters().peak_in_use, 10u);
  for (int i = 1; i < 10; i += 2) pool.free(v[static_cast<std::size_t>(i)]);
  EXPECT_EQ(pool.counters().frees, 10u);
  EXPECT_EQ(pool.counters().in_use, 0u);
  EXPECT_EQ(pool.counters().allocs - pool.counters().frees, 0u);
}

TEST(PacketPool, FreeHeadMiddleTailKeepsListConsistent) {
  PacketPool pool;
  Packet* a = pool.alloc();
  Packet* b = pool.alloc();
  Packet* c = pool.alloc();
  pool.free(b);
  pool.free(c);
  pool.free(a);
  EXPECT_EQ(pool.counters().in_use, 0u);
}

TEST(PacketPool, DefaultsAndLeftoversReleasedByDestructor) {
  PacketPool pool;
  Packet* p = pool.alloc();
  EXPECT_EQ(p->size, kWireBytes);
  EXPECT_EQ(p->seq, 0u);
  pool.alloc();
  EXPECT_EQ(pool.counters().in_use, 2u);  // destructor frees the rest (checked under ASan)
}
