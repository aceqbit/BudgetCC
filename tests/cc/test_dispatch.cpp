#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <type_traits>

#include "cc/dispatch.hpp"
#include "transport/sender.hpp"

using namespace bcc;

TEST(Dispatch, EveryControllerIsFinalSoSenderCallsResolveAtCompileTime) {
  static_assert(std::is_final<Reno>::value && std::is_final<Cubic>::value && std::is_final<BbrLite>::value &&
                    std::is_final<GccLite>::value && std::is_final<BudgetCc>::value,
                "E5 relies on final controller classes");
  SUCCEED();
}

TEST(Dispatch, BuildsTheNamedConcreteTypeAndForwardsTheResult) {
  const CcConfig cfg;
  for (const std::string& name : congestionControlNames()) {
    const std::string got = visitCongestionControl(name, cfg, [](auto cc) { return std::string(cc->name()); });
    EXPECT_EQ(got, name);
  }
  const bool is_cubic = visitCongestionControl("cubic", cfg, [](auto cc) {
    return std::is_same<decltype(cc), std::unique_ptr<Cubic>>::value;
  });
  EXPECT_TRUE(is_cubic);
}

TEST(Dispatch, UnknownNameAndUnknownParameterThrow) {
  CcConfig cfg;
  EXPECT_THROW(visitCongestionControl("nope", cfg, [](auto) { return 0; }), std::invalid_argument);
  cfg.params["bogus_key"] = 1.0;
  EXPECT_THROW(visitCongestionControl("reno", cfg, [](auto) { return 0; }), std::invalid_argument);
}

namespace {
struct Replay {
  std::uint64_t acked, lost, inflight;
  double rate;
  std::uint32_t cap;
};

template <class CC>
Replay replay(std::unique_ptr<CC> cc) {
  Simulator sim;
  PacketPool pool;
  BulkSource source;
  BasicSender<CC> s(sim, pool, 0, std::move(cc), source, [](Packet*) {});
  for (std::uint64_t i = 0; i < 400; ++i) s.trackSent(i, static_cast<Time>(i) * 1'000'000, kWireBytes);
  for (std::uint64_t i = 0; i < 400; ++i) {
    if (i % 17 == 5) continue;  // never acked: becomes a reordering loss
    const Time recv = static_cast<Time>(i) * 1'000'000 + 25'000'000 + static_cast<Time>(i % 7) * 1'000'000;
    s.onFeedbackAt(recv + 20'000'000, FeedbackItem{i, recv});
  }
  s.onControlTick(fromMs(600));
  return Replay{s.counters().acked_pkts, s.counters().lost_pkts, s.inflightBytes(), s.cc().pacingRateBps(),
                s.cc().inflightCapBytes()};
}
}  // namespace

TEST(Dispatch, DevirtualisedSenderMatchesTheVirtualOneOnAScriptedStream) {
  const CcConfig cfg;
  for (const std::string& name : congestionControlNames()) {
    const Replay a = visitCongestionControl(name, cfg, [](auto cc) { return replay(std::move(cc)); });
    const Replay b = replay(makeCongestionControl(name, cfg));
    EXPECT_EQ(a.acked, b.acked) << name;
    EXPECT_EQ(a.lost, b.lost) << name;
    EXPECT_EQ(a.inflight, b.inflight) << name;
    EXPECT_EQ(a.rate, b.rate) << name;
    EXPECT_EQ(a.cap, b.cap) << name;
    EXPECT_GT(a.lost, 0u) << name;
  }
}
