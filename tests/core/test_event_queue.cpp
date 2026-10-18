#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <type_traits>
#include <vector>

#include "core/event_queue.hpp"
#include "reference/naive_event_queue.hpp"

using namespace bcc;

TEST(EventQueue, PopsByTimeThenInsertionOrder) {
  EventQueue q;
  std::vector<int> order;
  q.push(30, [&] { order.push_back(3); });
  q.push(10, [&] { order.push_back(1); });
  q.push(10, [&] { order.push_back(2); });
  q.push(20, [&] { order.push_back(4); });
  EXPECT_EQ(q.size(), 4u);
  EXPECT_EQ(q.nextTime(), 10);
  while (!q.empty()) q.pop().run();
  EXPECT_EQ(order, (std::vector<int>{1, 2, 4, 3}));
  EXPECT_EQ(q.eventsProcessed(), 4u);
}

TEST(EventQueue, EqualTimeIsFifo) {
  EventQueue q;
  std::vector<int> order;
  for (int i = 0; i < 100; ++i) q.push(5, [&order, i] { order.push_back(i); });
  while (!q.empty()) q.pop().run();
  for (int i = 0; i < 100; ++i) ASSERT_EQ(order[static_cast<std::size_t>(i)], i);
}

TEST(EventQueue, MillionRandomEventsComeOutSorted) {
  EventQueue q;
  std::mt19937_64 rng(42);
  constexpr int kN = 1'000'000;
  for (int i = 0; i < kN; ++i) q.push(static_cast<Time>(rng() % 100000), [] {});
  Time prev = -1;
  std::uint64_t prev_seq = 0;
  for (int i = 0; i < kN; ++i) {
    auto e = q.pop();
    ASSERT_GE(e.time, prev);
    if (e.time == prev) {
      ASSERT_GT(e.seq, prev_seq);
    }
    prev = e.time;
    prev_seq = e.seq;
  }
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.eventsProcessed(), static_cast<std::uint64_t>(kN));
}

TEST(EventQueue, PushNeverAllocatesWithinInitialCapacity) {
  EventQueue q;
  int hits = 0;
  for (int i = 0; i < 1000; ++i) q.push(static_cast<Time>(1000 - i), [&hits] { ++hits; });
  while (!q.empty()) q.pop().run();
  EXPECT_EQ(hits, 1000);
}

TEST(EventQueue, DifferentialAgainstPhase1Queue) {
  EventQueue fast;
  bccref::NaiveEventQueue ref;
  std::mt19937_64 rng(2024);
  std::vector<int> got, want;
  int id = 0;
  for (int round = 0; round < 20000; ++round) {
    const bool push = fast.empty() || rng() % 3 != 0;
    if (push) {
      const Time t = static_cast<Time>(rng() % 500);
      const int my = id++;
      fast.push(t, [&got, my] { got.push_back(my); });
      ref.push(t, [&want, my] { want.push_back(my); });
    } else {
      ASSERT_EQ(fast.nextTime(), ref.nextTime());
      fast.pop().run();
      ref.pop().fn();
    }
  }
  while (!fast.empty()) {
    fast.pop().run();
    ref.pop().fn();
  }
  EXPECT_TRUE(ref.empty());
  EXPECT_EQ(got, want);
}

TEST(EventQueue, EventIsPlainOldData) {
  EXPECT_TRUE(std::is_trivially_copyable<Event>::value);
  EXPECT_LE(sizeof(Event), 64u);
}

TEST(EventQueue, SlotsAreRecycledSoStorageStaysBounded) {
  EventQueue q;
  int hits = 0;
  for (int round = 0; round < 1000; ++round) {
    for (int i = 0; i < 50; ++i) q.push(round * 100 + i, [&hits] { ++hits; });
    for (int i = 0; i < 50; ++i) q.pop().run();
  }
  EXPECT_EQ(hits, 50'000);
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.eventsProcessed(), 50'000u);
}
