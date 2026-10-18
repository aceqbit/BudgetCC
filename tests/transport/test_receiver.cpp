#include <gtest/gtest.h>

#include <vector>

#include "transport/receiver.hpp"

using namespace bcc;

namespace {
struct Rig {
  Simulator sim;
  PacketPool pool;
  std::vector<std::pair<FeedbackItem, Time>> fb;  // (item, time it reached the sender)
  Receiver rx;

  explicit Rig(std::uint32_t flow = 5)
      : rx(sim, pool, flow, fromMs(20), [this](const FeedbackItem& f) { fb.emplace_back(f, sim.now()); }) {}

  Packet* make(std::uint64_t seq, Time send_ts, std::uint32_t ssrc = 5) {
    Packet* p = pool.alloc();
    p->seq = seq;
    p->send_ts = send_ts;
    RtpHeader h;
    h.seq = static_cast<std::uint16_t>(seq);
    h.ssrc = ssrc;
    encodeRtp(h, p->hdr.data());
    return p;
  }
};
}  // namespace

TEST(Receiver, FeedbackReturnsAfterReturnDelayWithRecvTime) {
  Rig r;
  r.sim.runUntil(fromMs(100));
  r.rx.onPacket(r.make(0, fromMs(60)));
  r.sim.runUntil(fromMs(131));
  ASSERT_EQ(r.fb.size(), 1u);
  EXPECT_EQ(r.fb[0].first.seq, 0u);
  EXPECT_EQ(r.fb[0].first.recv_ts, fromMs(100));
  EXPECT_EQ(r.fb[0].second, fromMs(130));  // next 10 ms boundary (110) + 20 ms
}

TEST(Receiver, PacketsOfOneIntervalShareOneFeedbackBatch) {
  Rig r;
  r.sim.runUntil(fromMs(101));
  r.rx.onPacket(r.make(0, fromMs(60)));
  r.sim.runUntil(fromMs(104));
  r.rx.onPacket(r.make(1, fromMs(60)));
  r.sim.runUntil(fromMs(111));
  r.rx.onPacket(r.make(2, fromMs(60)));  // next interval
  r.sim.runUntil(fromMs(200));
  ASSERT_EQ(r.fb.size(), 3u);
  EXPECT_EQ(r.fb[0].second, fromMs(130));  // boundary 110 + 20
  EXPECT_EQ(r.fb[1].second, fromMs(130));
  EXPECT_EQ(r.fb[2].second, fromMs(140));  // boundary 120 + 20
  EXPECT_EQ(r.fb[0].first.recv_ts, fromMs(101));  // items keep their own receive times
  EXPECT_EQ(r.fb[1].first.recv_ts, fromMs(104));
  EXPECT_EQ(r.fb[2].first.recv_ts, fromMs(111));
  EXPECT_EQ(r.fb[0].first.seq, 0u);
  EXPECT_EQ(r.fb[2].first.seq, 2u);
}

TEST(Receiver, ArrivalExactlyOnABoundaryWaitsForTheNextOne) {
  Rig r;
  r.sim.runUntil(fromMs(100));
  r.rx.onPacket(r.make(0, fromMs(60)));
  r.sim.runUntil(fromMs(200));
  ASSERT_EQ(r.fb.size(), 1u);
  EXPECT_EQ(r.fb[0].second, fromMs(130));
}

TEST(Receiver, OneFeedbackEventPerIntervalNotPerPacket) {
  Rig r;
  const std::uint64_t before = r.sim.eventsProcessed();
  for (std::uint64_t s = 0; s < 50; ++s) r.rx.onPacket(r.make(s, 0));  // all at t = 0
  r.sim.runUntil(fromMs(100));
  EXPECT_EQ(r.fb.size(), 50u);
  EXPECT_EQ(r.sim.eventsProcessed() - before, 2u);  // one flush + one delivery
}

TEST(Receiver, RecordsOwdAndCounters) {
  Rig r;
  r.sim.runUntil(fromMs(200));
  r.rx.onPacket(r.make(0, fromMs(180)));  // 20 ms
  r.rx.onPacket(r.make(1, fromMs(100)));  // 100 ms (not above)
  r.rx.onPacket(r.make(2, fromMs(99)));   // 101 ms
  r.rx.onPacket(r.make(3, fromMs(49)));   // 151 ms
  r.rx.onPacket(r.make(4, fromMs(-101))); // 301 ms
  const auto& c = r.rx.counters();
  EXPECT_EQ(c.recv_pkts, 5u);
  EXPECT_EQ(c.recv_bytes, 5u * kWireBytes);
  EXPECT_EQ(c.over_100ms, 3u);
  EXPECT_EQ(c.over_150ms, 2u);
  EXPECT_EQ(c.over_300ms, 1u);
  ASSERT_EQ(r.rx.owdNs().size(), 5u);
  EXPECT_EQ(r.rx.owdNs()[0], fromMs(20));
  EXPECT_EQ(r.rx.owdNs()[4], fromMs(301));
  EXPECT_EQ(r.pool.counters().in_use, 0u);  // receiver frees every packet
}

TEST(Receiver, UnwrapsSequenceAcrossWrap) {
  Rig r;
  for (std::uint64_t s : {65534ULL, 65535ULL, 65536ULL, 65537ULL}) r.rx.onPacket(r.make(s, 0));
  r.sim.runUntil(fromMs(40));
  ASSERT_EQ(r.fb.size(), 4u);
  EXPECT_EQ(r.fb[2].first.seq, 65536u);
  EXPECT_EQ(r.fb[3].first.seq, 65537u);
}

TEST(Receiver, RejectsBadHeaderAndForeignSsrc) {
  Rig r;
  Packet* bad = r.pool.alloc();  // all-zero header: version 0
  r.rx.onPacket(bad);
  r.rx.onPacket(r.make(0, 0, /*ssrc=*/6));
  EXPECT_EQ(r.rx.counters().bad_packets, 2u);
  EXPECT_EQ(r.rx.counters().recv_pkts, 0u);
  r.sim.runUntil(fromMs(50));
  EXPECT_TRUE(r.fb.empty());
  EXPECT_EQ(r.pool.counters().in_use, 0u);
}

TEST(Receiver, IntervalAccumulatorsResetOnTake) {
  Rig r;
  r.sim.runUntil(fromMs(50));
  r.rx.onPacket(r.make(0, fromMs(30)));
  r.rx.onPacket(r.make(1, fromMs(10)));
  auto iv = r.rx.takeInterval();
  EXPECT_EQ(iv.pkts, 2u);
  EXPECT_EQ(iv.bytes, 2u * kWireBytes);
  EXPECT_DOUBLE_EQ(iv.owd_ms_sum, 20.0 + 40.0);
  auto again = r.rx.takeInterval();
  EXPECT_EQ(again.pkts, 0u);
  EXPECT_EQ(again.bytes, 0u);
}
