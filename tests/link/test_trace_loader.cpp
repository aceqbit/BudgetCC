#include <gtest/gtest.h>

#include <stdexcept>

#include "link/trace_loader.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

TEST(TraceLoader, ParsesAndCountsDuplicates) {
  Trace t = Trace::parse("1\n1\n3\n10\n", "t");
  EXPECT_EQ(t.period(), fromMs(10));
  EXPECT_EQ(t.opportunitiesPerCycle(), 4u);
  Trace::Cursor c(t);
  auto a = c.next();
  EXPECT_EQ(a.time, fromMs(1));
  EXPECT_EQ(a.count, 2u);
  EXPECT_EQ(c.next().time, fromMs(3));
  EXPECT_EQ(c.next().time, fromMs(10));
  auto wrap = c.next();  // second cycle
  EXPECT_EQ(wrap.time, fromMs(11));
  EXPECT_EQ(wrap.count, 2u);
}

TEST(TraceLoader, ToleratesCrlfAndBlankLines) {
  Trace t = Trace::parse("2\r\n\r\n5\r\n", "t");
  EXPECT_EQ(t.opportunitiesPerCycle(), 2u);
}

TEST(TraceLoader, RejectsBadInputWithMessage) {
  auto msg = [](const char* text) {
    try {
      Trace::parse(text, "bad");
    } catch (const std::runtime_error& e) {
      return std::string(e.what());
    }
    return std::string("no throw");
  };
  EXPECT_EQ(msg("1\n5\n3\n"), "bad:3: timestamps must be non-decreasing");
  EXPECT_EQ(msg("1\nabc\n"), "bad:2: not a non-negative integer: 'abc'");
  EXPECT_EQ(msg("1\n2x\n"), "bad:2: not a non-negative integer: '2x'");
  EXPECT_EQ(msg("-4\n"), "bad:1: negative timestamp");
  EXPECT_EQ(msg(""), "bad: empty trace");
  EXPECT_EQ(msg("\n\n"), "bad: empty trace");
  EXPECT_THROW(Trace::load("/definitely/not/here.trace"), std::runtime_error);
}

TEST(TraceLoader, CapacityBytesIsExactFor12Mbps) {
  Trace t = Trace::parse(bcc_test::constantTrace(12.0, 1000), "12mbps");
  // one opportunity per ms -> 1000 per second
  EXPECT_EQ(t.opportunities(fromSec(1), fromSec(2)), 1000u);
  EXPECT_EQ(t.capacityBytes(fromSec(1), fromSec(2)), 1'500'000u);
  EXPECT_EQ(t.capacityBytes(fromSec(5), fromSec(15)), 15'000'000u);
  EXPECT_EQ(t.capacityBytes(fromSec(3), fromSec(3)), 0u);
  EXPECT_EQ(t.capacityBytes(fromSec(4), fromSec(3)), 0u);
}

TEST(TraceLoader, CapacityMatchesCursorEnumerationAcrossLoops) {
  Trace t = Trace::parse("2\n2\n5\n9\n10\n", "t");
  const Time t0 = fromMs(3), t1 = fromMs(47);
  std::uint64_t n = 0;
  Trace::Cursor c(t);
  for (;;) {
    auto o = c.next();
    if (o.time >= t1) break;
    if (o.time >= t0) n += o.count;
  }
  EXPECT_EQ(t.opportunities(t0, t1), n);
  EXPECT_GT(n, 10u);
}

TEST(TraceLoader, CapacityTimesOneCycleBoundary) {
  Trace t = Trace::parse("1\n2\n3\n", "t");
  Trace::Cursor c(t);
  for (int boundary : {0, 1, 2, 3, 4, 6, 7}) {
    std::uint64_t n = 0;
    Trace::Cursor cc(t);
    for (;;) {
      auto o = cc.next();
      if (o.time >= fromMs(boundary)) break;
      n += o.count;
    }
    EXPECT_EQ(t.opportunities(0, fromMs(boundary)), n) << boundary;
  }
}
