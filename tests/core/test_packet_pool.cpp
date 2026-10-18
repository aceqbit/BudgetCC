#include <gtest/gtest.h>

#include <stdexcept>
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

TEST(PacketPool, FreedPacketsAreReusedWithoutNewSlabs) {
  PacketPool pool;
  Packet* a = pool.alloc();
  const std::uint64_t slabs = pool.counters().slabs;
  pool.free(a);
  Packet* b = pool.alloc();
  EXPECT_EQ(a, b);
  EXPECT_EQ(pool.counters().slabs, slabs);
  EXPECT_EQ(b->seq, 0u);  // recycled packets come back default-initialised
}

TEST(PacketPool, ReserveCoversRequestAndExhaustionAddsASlab) {
  PacketPool pool;
  pool.reserve(3000);
  const std::uint64_t slabs = pool.counters().slabs;
  EXPECT_GE(slabs * PacketPool::kSlabPackets, 3000u);
  std::vector<Packet*> v;
  for (std::size_t i = 0; i < slabs * PacketPool::kSlabPackets; ++i) v.push_back(pool.alloc());
  EXPECT_EQ(pool.counters().slabs, slabs);  // no heap use inside the reservation
  v.push_back(pool.alloc());                  // exhaustion path
  EXPECT_EQ(pool.counters().slabs, slabs + 1);
  EXPECT_EQ(pool.counters().in_use, v.size());
  for (Packet* p : v) pool.free(p);
  EXPECT_EQ(pool.counters().in_use, 0u);
}

TEST(PacketPool, DoubleFreeThrows) {
  PacketPool pool;
  Packet* p = pool.alloc();
  pool.free(p);
  EXPECT_THROW(pool.free(p), std::logic_error);
  EXPECT_EQ(pool.counters().frees, 1u);
}

TEST(PacketPool, DefaultsAreSet) {
  PacketPool pool;
  Packet* p = pool.alloc();
  EXPECT_EQ(p->size, kWireBytes);
  EXPECT_EQ(p->seq, 0u);
  pool.free(p);
}
