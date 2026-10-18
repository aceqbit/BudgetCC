#include <gtest/gtest.h>

#include <vector>

#include "support/fake_cc.hpp"
#include "transport/rtp_header.hpp"
#include "transport/sender.hpp"

using namespace bcc;
using bcc_test::FakeCc;

namespace {
struct Rig {
  Simulator sim;
  PacketPool pool;
  BulkSource source;
  std::vector<Packet> sent;  // copies of what left the sender
  FakeCc* cc;
  std::unique_ptr<Sender> sender;

  Rig(double rate_bps, std::uint32_t cap) {
    auto fake = std::make_unique<FakeCc>(rate_bps, cap);
    cc = fake.get();
    sender = std::make_unique<Sender>(sim, pool, 9, std::move(fake), source, [this](Packet* p) {
      sent.push_back(*p);
      pool.free(p);
    });
    sender->start(0);
  }
  void ack(Time now, std::uint64_t seq, Time recv_ts) { sender->onFeedbackAt(now, FeedbackItem{seq, recv_ts}); }
};
}  // namespace

TEST(Sender, SequenceMonotoneSendTimestampedAndHeaderValid) {
  Rig r(10e6, 1'000'000);  // 1250 B at 10 Mbps = 1 ms
  r.sim.runUntil(fromMs(10) + 1);
  ASSERT_EQ(r.sent.size(), 11u);
  for (std::size_t i = 0; i < r.sent.size(); ++i) {
    EXPECT_EQ(r.sent[i].seq, i);
    EXPECT_EQ(r.sent[i].send_ts, static_cast<Time>(i) * fromMs(1));
    EXPECT_EQ(r.sent[i].flow, 9u);
    RtpHeader h;
    ASSERT_TRUE(decodeRtp(r.sent[i].hdr.data(), 12, h));
    EXPECT_EQ(h.seq, static_cast<std::uint16_t>(i));
    EXPECT_EQ(h.ssrc, 9u);
  }
  EXPECT_EQ(r.sender->counters().sent_pkts, 11u);
  EXPECT_EQ(r.sender->counters().sent_bytes, 11u * kWireBytes);
}

TEST(Sender, BlocksAtInflightCapAndUnblocksOnAck) {
  Rig r(1e9, 3 * kWireBytes);
  r.sim.runUntil(fromMs(5));
  EXPECT_EQ(r.sent.size(), 3u);
  EXPECT_EQ(r.sender->inflightBytes(), 3u * kWireBytes);
  r.ack(fromMs(6), 0, fromMs(3));
  EXPECT_EQ(r.sent.size(), 4u);  // one slot freed -> exactly one more packet
  EXPECT_EQ(r.sender->inflightBytes(), 3u * kWireBytes);
}

TEST(Sender, ControlTickRunsEvery10msAndAppliesNewRate) {
  Rig r(1e6, 1'000'000);  // 1250 B at 1 Mbps = 10 ms
  r.sim.runUntil(fromMs(35));
  EXPECT_EQ(r.cc->timers, (std::vector<Time>{fromMs(10), fromMs(20), fromMs(30)}));
  const auto before = r.sent.size();
  r.cc->setRate(100e6);  // picked up by the tick at 40 ms
  r.sim.runUntil(fromMs(45));
  EXPECT_GT(r.sent.size(), before + 40u);  // 100 Mbps = one packet every 0.1 ms
}

TEST(Sender, LossAfterThreeLaterPacketsAcked) {
  Rig r(1e9, 1'000'000);
  r.sim.runUntil(fromMs(1));
  ASSERT_GE(r.sent.size(), 10u);
  r.ack(fromMs(50), 1, fromMs(30));
  r.ack(fromMs(50), 2, fromMs(30));
  EXPECT_TRUE(r.cc->losses.empty());
  r.ack(fromMs(50), 3, fromMs(30));
  ASSERT_EQ(r.cc->losses.size(), 1u);
  EXPECT_EQ(r.cc->losses[0].seq, 0u);
  EXPECT_FALSE(r.cc->losses[0].timeout);
  EXPECT_EQ(r.cc->losses[0].bytes, kWireBytes);
  EXPECT_EQ(r.sender->counters().lost_pkts, 1u);
  EXPECT_EQ(r.sender->counters().timeouts, 0u);
  EXPECT_EQ(r.sender->counters().acked_pkts, 3u);
}

TEST(Sender, NoRetransmissionOfLostPacket) {
  Rig r(1e9, 20 * kWireBytes);
  r.sim.runUntil(fromMs(1));
  for (std::uint64_t s = 1; s <= 4; ++s) r.ack(fromMs(50), s, fromMs(30));
  r.sim.runUntil(fromMs(2));
  std::vector<std::uint64_t> seqs;
  for (const auto& p : r.sent) seqs.push_back(p.seq);
  for (std::size_t i = 1; i < seqs.size(); ++i) EXPECT_EQ(seqs[i], seqs[i - 1] + 1);  // strictly new sequence numbers
}

TEST(Sender, TimeoutIs200msWithoutRttSample) {
  Rig r(1e6, 2 * kWireBytes);  // two packets in flight, then blocked
  r.sim.runUntil(fromMs(200));
  EXPECT_TRUE(r.cc->losses.empty());  // packet 0 sent at t=0: age 190 ms at the 190 ms tick
  r.sim.runUntil(fromMs(215));
  ASSERT_FALSE(r.cc->losses.empty());
  EXPECT_TRUE(r.cc->losses[0].timeout);
  EXPECT_EQ(r.cc->losses[0].seq, 0u);
  EXPECT_EQ(r.cc->losses[0].now, fromMs(210));  // first 10 ms tick where age > 200 ms
  EXPECT_GE(r.sender->counters().timeouts, 1u);
}

TEST(Sender, TimeoutIsTwiceSrttWhenLarger) {
  Rig r(10e6, 1'000'000);
  r.sim.runUntil(fromMs(2));
  r.ack(fromMs(1000), 0, fromMs(5));  // rtt 1000 ms -> srtt 1000 ms -> timeout 2000 ms
  EXPECT_EQ(r.sender->srtt(), fromMs(1000));
  r.sender->trackSent(500, fromMs(1000), kWireBytes);
  r.cc->losses.clear();
  auto lost500 = [&] {
    for (const auto& l : r.cc->losses)
      if (l.seq == 500 && l.timeout) return true;
    return false;
  };
  r.sender->onControlTick(fromMs(3000));  // age == 2 x srtt: not yet
  EXPECT_FALSE(lost500());
  r.sender->onControlTick(fromMs(3000) + 1);
  EXPECT_TRUE(lost500());
}

TEST(Sender, RttOwdAndSmoothing) {
  Rig r(10e6, 1'000'000);
  r.sim.runUntil(fromMs(3));
  r.ack(fromMs(50), 0, fromMs(30));  // sent at 0
  ASSERT_EQ(r.cc->acks.size(), 1u);
  EXPECT_EQ(r.cc->acks[0].rtt, fromMs(50));
  EXPECT_EQ(r.cc->acks[0].owd, fromMs(30));
  EXPECT_EQ(r.cc->acks[0].send_ts, 0);
  EXPECT_EQ(r.cc->acks[0].recv_ts, fromMs(30));
  EXPECT_EQ(r.cc->acks[0].bytes, kWireBytes);
  EXPECT_EQ(r.sender->srtt(), fromMs(50));
  r.ack(fromMs(91), 1, fromMs(35));  // sent at 1 ms: rtt 90 ms
  EXPECT_EQ(r.sender->srtt(), fromMs(50) + (fromMs(90) - fromMs(50)) / 8);
}

TEST(Sender, DuplicateOrLateAckIsIgnored) {
  Rig r(1e9, 20 * kWireBytes);
  r.sim.runUntil(fromMs(1));
  for (std::uint64_t s = 1; s <= 3; ++s) r.ack(fromMs(50), s, fromMs(30));  // declares 0 lost
  ASSERT_EQ(r.cc->losses.size(), 1u);
  const auto acked = r.sender->counters().acked_pkts;
  r.ack(fromMs(60), 0, fromMs(30));  // late ack for a packet already declared lost
  r.ack(fromMs(60), 1, fromMs(30));  // duplicate
  EXPECT_EQ(r.sender->counters().acked_pkts, acked);
  EXPECT_EQ(r.cc->losses.size(), 1u);
}
