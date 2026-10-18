#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <vector>

#include "core/simulator.hpp"

using namespace bcc;

namespace {
std::uint64_t scenarioHash(std::uint64_t seed, std::uint64_t* events) {
  Simulator sim;
  std::mt19937_64 rng(seed);
  for (int i = 0; i < 1000; ++i) {
    const Time t = static_cast<Time>(rng() % 5000);
    sim.schedule(t, [&sim, &rng] {
      if (rng() % 4 == 0) sim.scheduleIn(static_cast<Duration>(rng() % 100), [] {});
    });
  }
  sim.runUntil(10'000);
  *events = sim.eventsProcessed();
  return sim.traceHash();
}
}  // namespace

TEST(Simulator, TimeNeverDecreases) {
  Simulator sim;
  std::vector<Time> seen;
  for (Time t : {50, 10, 30, 10, 40}) sim.schedule(t, [&] { seen.push_back(sim.now()); });
  sim.runUntil(100);
  EXPECT_EQ(seen, (std::vector<Time>{10, 10, 30, 40, 50}));
  EXPECT_EQ(sim.now(), 100);
}

TEST(Simulator, RunUntilStopsAtBoundExclusive) {
  Simulator sim;
  int ran = 0;
  sim.schedule(99, [&] { ++ran; });
  sim.schedule(100, [&] { ++ran; });
  sim.schedule(101, [&] { ++ran; });
  sim.runUntil(100);
  EXPECT_EQ(ran, 1);
  EXPECT_EQ(sim.now(), 100);
  EXPECT_EQ(sim.eventsProcessed(), 1u);
  EXPECT_FALSE(sim.idle());
}

TEST(Simulator, SchedulingInThePastThrows) {
  Simulator sim;
  sim.schedule(10, [] {});
  sim.runUntil(20);
  EXPECT_THROW(sim.schedule(19, [] {}), std::logic_error);
  EXPECT_NO_THROW(sim.schedule(20, [] {}));
}

TEST(Simulator, SameInputSameTraceHash) {
  std::uint64_t e1 = 0, e2 = 0, e3 = 0;
  const auto h1 = scenarioHash(7, &e1);
  const auto h2 = scenarioHash(7, &e2);
  const auto h3 = scenarioHash(8, &e3);
  EXPECT_EQ(h1, h2);
  EXPECT_EQ(e1, e2);
  EXPECT_NE(h1, h3);
  EXPECT_GE(e1, 1000u);
}

TEST(Simulator, EventsMayScheduleMoreEvents) {
  Simulator sim;
  int n = 0;
  std::function<void()> tick = [&] {
    if (++n < 5) sim.scheduleIn(10, [&tick] { tick(); });
  };
  sim.schedule(0, [&tick] { tick(); });
  sim.runUntil(1000);
  EXPECT_EQ(n, 5);
  EXPECT_EQ(sim.eventsProcessed(), 5u);
}
