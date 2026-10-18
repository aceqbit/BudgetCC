#include <gtest/gtest.h>

#include <vector>

#include "link/bottleneck_link.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

namespace {
struct Rig {
  Simulator sim;
  PacketPool pool;
  Trace trace;
  std::vector<std::pair<std::uint64_t, Time>> delivered;  // (seq, recv time)
  BottleneckLink link;

  Rig(const std::string& trace_text, std::uint64_t limit_bytes, Duration prop = fromMs(20))
      : trace(Trace::parse(trace_text, "t")),
        link(sim, pool, trace, limit_bytes, prop, [this](Packet* p) {
          delivered.emplace_back(p->seq, sim.now());
          pool.free(p);
        }) {
    link.start();
  }
  bool send(std::uint64_t seq) {
    Packet* p = pool.alloc();
    p->seq = seq;
    return link.onArrival(p);
  }
};
}  // namespace

TEST(BottleneckLink, CreditCarriesWhileBackloggedAndResetsWhenEmpty) {
  Rig r("10\n20\n30\n40\n50\n", 1'000'000, 0);
  for (std::uint64_t i = 0; i < 3; ++i) r.send(i);
  r.sim.runUntil(fromMs(45));
  // credit: 1500 -> pkt0 (250 left), 1750 -> pkt1 (500), 2000 -> pkt2 (750) then empty -> reset
  ASSERT_EQ(r.delivered.size(), 3u);
  EXPECT_EQ(r.delivered[0].second, fromMs(10));
  EXPECT_EQ(r.delivered[1].second, fromMs(20));
  EXPECT_EQ(r.delivered[2].second, fromMs(30));
  EXPECT_EQ(r.link.counters().wasted_opps, 1u);  // t=40 found an empty queue
  // after the reset a new packet needs a full opportunity again: 1500 >= 1250 departs at t=50
  r.send(3);
  r.sim.runUntil(fromMs(60));
  ASSERT_EQ(r.delivered.size(), 4u);
  EXPECT_EQ(r.delivered[3].second, fromMs(50));
  EXPECT_EQ(r.link.counters().granted_bytes, 4u * kOpportunityBytes);
}

TEST(BottleneckLink, CreditDoesNotAccumulateAcrossIdleOpportunities) {
  Rig r("10\n20\n30\n", 1'000'000, 0);
  r.sim.runUntil(fromMs(25));  // opportunities at 10, 20 wasted
  EXPECT_EQ(r.link.counters().wasted_opps, 2u);
  for (std::uint64_t i = 0; i < 3; ++i) r.send(i);
  r.sim.runUntil(fromMs(35));
  // only one opportunity's credit (1500 B) is available: exactly one packet leaves
  EXPECT_EQ(r.delivered.size(), 1u);
  EXPECT_EQ(r.link.queuedPackets(), 2u);
}

TEST(BottleneckLink, DropTailAtLimitAndLimitPlusOneByte) {
  Rig at_limit("100\n", 3 * kWireBytes);
  EXPECT_TRUE(at_limit.send(0));
  EXPECT_TRUE(at_limit.send(1));
  EXPECT_TRUE(at_limit.send(2));  // 3750 <= 3750
  EXPECT_FALSE(at_limit.send(3));
  EXPECT_EQ(at_limit.link.counters().dropped_pkts, 1u);
  EXPECT_EQ(at_limit.link.counters().max_queue_bytes, 3u * kWireBytes);

  Rig below("100\n", 3 * kWireBytes - 1);
  EXPECT_TRUE(below.send(0));
  EXPECT_TRUE(below.send(1));
  EXPECT_FALSE(below.send(2));  // 3750 > 3749
}

TEST(BottleneckLink, FifoOrderAndPropagationDelay) {
  Rig r(bcc_test::constantTrace(12.0, 1000), 1'000'000, fromMs(20));
  for (std::uint64_t i = 0; i < 50; ++i) r.send(i);
  r.sim.runUntil(fromMs(500));
  ASSERT_EQ(r.delivered.size(), 50u);
  for (std::size_t i = 0; i < 50; ++i) EXPECT_EQ(r.delivered[i].first, i);
  EXPECT_GE(r.delivered[0].second, fromMs(21));  // first opportunity at 1 ms + 20 ms propagation
  EXPECT_EQ(r.delivered[0].second, fromMs(21));
}

TEST(BottleneckLink, ConservationAtEndOfRun) {
  Rig r(bcc_test::constantTrace(6.0, 1000), 20 * kWireBytes);
  std::uint64_t sent = 0;
  for (; sent < 200; ++sent) r.send(sent);
  r.sim.runUntil(fromMs(300));
  const auto& c = r.link.counters();
  EXPECT_EQ(sent, c.delivered_pkts + c.dropped_pkts + r.link.queuedPackets());
  EXPECT_LE(c.delivered_bytes, r.trace.capacityBytes(0, r.sim.now()));
  EXPECT_EQ(c.arrived_pkts, sent);
}

TEST(BottleneckLink, QueueDelayIsGroundTruth) {
  Rig r("50\n100\n150\n", 1'000'000, 0);
  EXPECT_EQ(r.link.queueDelay(0), 0);
  r.send(0);
  r.sim.runUntil(fromMs(30));
  EXPECT_EQ(r.link.queueDelay(r.sim.now()), fromMs(30));  // head age while waiting
  r.sim.runUntil(fromMs(60));
  EXPECT_EQ(r.link.queueDelay(r.sim.now()), 0);  // empty again
}

TEST(BottleneckLink, CapacityStepDelaySpikeMatchesArithmetic) {
  // 12 Mbps until t = 10 s then 3 Mbps; sender offers 1190 pkt/s (11.9 Mbps) throughout.
  Rig r(bcc_test::rateTrace({{0, 12.0}, {10000, 3.0}}, 30000), 100'000'000, 0);
  std::uint64_t seq = 0;
  const Duration gap = fromSec(1) / 1190;
  const Time stop = fromSec(12);
  std::function<void()> tick = [&] {
    if (r.sim.now() >= stop) return;
    r.send(seq++);
    r.sim.scheduleIn(gap, [&tick] { tick(); });
  };
  r.sim.schedule(0, [&tick] { tick(); });
  r.sim.runUntil(stop);
  const double queue_bytes = static_cast<double>(r.link.queuedBytes());
  ASSERT_GT(queue_bytes, 500'000.0);
  const double expected_s = queue_bytes / (3e6 / 8.0);
  // find when the queue empties
  Time drained = -1;
  while (r.sim.now() < fromSec(25)) {
    r.sim.runUntil(r.sim.now() + fromMs(1));
    if (r.link.queuedPackets() == 0) {
      drained = r.sim.now();
      break;
    }
  }
  ASSERT_GT(drained, 0);
  const double observed_s = toSec(drained - stop);
  EXPECT_NEAR(observed_s, expected_s, 0.05 * expected_s);
}

TEST(BottleneckLink, BackloggedDeliversExactlyCapacity) {
  Rig r(bcc_test::constantTrace(12.0, 1000), 1'000'000'000);
  for (std::uint64_t i = 0; i < 4000; ++i) r.send(i);
  r.sim.runUntil(fromSec(1));
  const std::uint64_t at1 = r.link.counters().delivered_bytes;
  r.sim.runUntil(fromSec(2));
  const std::uint64_t d = r.link.counters().delivered_bytes - at1;
  EXPECT_NEAR(static_cast<double>(d), 1.5e6, kWireBytes);
}

TEST(BottleneckLink, IdenticalInputGivesIdenticalEventHash) {
  auto run = [] {
    Rig r(bcc_test::constantTrace(12.0, 1000), 100 * kWireBytes);
    for (std::uint64_t i = 0; i < 300; ++i) r.send(i);
    r.sim.runUntil(fromSec(1));
    return std::make_pair(r.sim.traceHash(), r.link.counters().delivered_bytes);
  };
  EXPECT_EQ(run(), run());
}
