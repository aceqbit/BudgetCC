// The spec's Phase 1 sanity table, end to end through sender, link, receiver and controller.
#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "harness/results.hpp"
#include "harness/runner.hpp"
#include "support/fake_cc.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

namespace {
const Trace& const12() {
  static const Trace t = Trace::parse(bcc_test::constantTrace(12.0, 60000), "const12");
  return t;
}
RunConfig cfg(const std::string& algo, std::uint32_t buffer) {
  RunConfig c;
  c.algos = {algo};
  c.trace = "const12";
  c.buffer_pkts = buffer;
  return c;
}
}  // namespace

TEST(Sanity, CubicDeepBufferFillsLinkAndReproducesBufferbloat) {
  Experiment e(cfg("cubic", 1000), const12());
  e.runUntil(fromSec(60));
  const RunResult r = e.finish();
  EXPECT_GE(r.summary.utilisation_pct, 90.0);
  const double max_q = *std::max_element(e.queueSeries().begin(), e.queueSeries().end());
  EXPECT_GE(max_q, 0.8 * (1'250'000.0 / 1'500'000.0) * 1000.0);  // >= 666 ms
  EXPECT_GT(r.summary.p95_owd_ms, 200.0);
}

TEST(Sanity, CubicShallowBufferShowsSawtoothWithPeriodicLoss) {
  std::vector<Sample> samples;
  RunConfig c = cfg("cubic", 100);
  Experiment e(c, const12(), 0, [&](const Sample& s) { samples.push_back(s); });
  e.runUntil(fromSec(60));
  const RunResult r = e.finish();
  int events = 0;
  int last_loss_ms = -10'000;
  for (const auto& s : samples) {
    if (s.lost > 0) {
      if (s.t_ms - last_loss_ms > 500) ++events;
      last_loss_ms = s.t_ms;
    }
  }
  EXPECT_GE(events, 3);
  EXPECT_GT(r.dropped_pkts, 0u);
  // sawtooth: the window repeatedly falls well below its running peak
  double peak = 0.0;
  int drops = 0;
  for (const auto& s : samples) {
    if (s.t_ms < 5000) continue;
    peak = std::max(peak, s.cwnd_kb);
    if (s.cwnd_kb < 0.75 * peak) {
      ++drops;
      peak = s.cwnd_kb;
    }
  }
  EXPECT_GE(drops, 3);
}

TEST(Sanity, RenoFillsLinkAndQueueReachesBufferLimit) {
  Experiment e(cfg("reno", 1000), const12());
  e.runUntil(fromSec(60));
  const RunResult r = e.finish();
  EXPECT_GE(r.summary.utilisation_pct, 90.0);
  EXPECT_GE(e.link().counters().max_queue_bytes, static_cast<std::uint64_t>(0.95 * 1'250'000));
  EXPECT_GT(r.dropped_pkts, 0u);
}

TEST(Sanity, StepDownDelaySpikeMatchesQueueOverNewRate) {
  // 12 -> 3 Mbps at t = 10 s with a sender offering 11.9 Mbps until t = 12 s, then going silent: the queue
  // that built up must drain at the new rate, so the spike lasts queue_bytes / 3 Mbps.
  const Trace step = Trace::parse(bcc_test::rateTrace({{0, 12.0}, {10000, 3.0}}, 60000), "step");
  Simulator sim;
  PacketPool pool;
  BulkSource src;
  std::unique_ptr<Receiver> rx;
  std::unique_ptr<BottleneckLink> link;
  std::unique_ptr<Sender> tx;
  link = std::make_unique<BottleneckLink>(sim, pool, step, 1'000'000'000, fromMs(20),
                                          [&](Packet* p) { rx->onPacket(p); });
  rx = std::make_unique<Receiver>(sim, pool, 0, fromMs(20), [&](const FeedbackItem& f) { tx->onFeedback(f); });
  auto fake = std::make_unique<bcc_test::FakeCc>(11.9e6, 2'000'000'000U);
  bcc_test::FakeCc* cc = fake.get();
  tx = std::make_unique<Sender>(sim, pool, 0, std::move(fake), src, [&](Packet* p) { link->onArrival(p); });
  link->start();
  tx->start(0);
  sim.schedule(fromSec(12), [&] { cc->setRate(Pacer::kMinRateBps); });
  sim.runUntil(fromSec(12) + fromMs(25));  // let the 10 ms control tick apply the rate; sender goes quiet
  const double queue_bytes = static_cast<double>(link->queuedBytes());
  ASSERT_GT(queue_bytes, 500'000.0);
  const double expected_s = queue_bytes / (3e6 / 8.0);
  Time drained = 0;
  const Time t_check = sim.now();
  while (sim.now() < fromSec(30)) {
    sim.runUntil(sim.now() + fromMs(1));
    if (link->queuedPackets() == 0) {
      drained = sim.now();
      break;
    }
  }
  ASSERT_GT(drained, 0);
  EXPECT_NEAR(toSec(drained - t_check), expected_s, 0.10 * expected_s);
}

TEST(Sanity, SameConfigurationTwiceIsByteIdentical) {
  RunConfig c = cfg("cubic", 1000);
  c.duration = fromSec(20);
  EXPECT_EQ(runResultToJson(runOnce(c, const12())), runResultToJson(runOnce(c, const12())));
}

TEST(Sanity, ByteConservationAtTheEndOfEveryRun) {
  for (const char* algo : {"reno", "cubic"})
    for (std::uint32_t buf : {100U, 1000U}) {
      RunConfig c = cfg(algo, buf);
      c.duration = fromSec(10);
      Experiment e(c, const12());
      e.runUntil(c.duration);
      EXPECT_NO_THROW(e.finish()) << algo << " " << buf;
      const auto& lc = e.link().counters();
      EXPECT_LE(lc.delivered_bytes, const12().capacityBytes(0, c.duration));
      EXPECT_EQ(e.packetsSent(), lc.delivered_pkts + lc.dropped_pkts + e.link().queuedPackets());
    }
}
