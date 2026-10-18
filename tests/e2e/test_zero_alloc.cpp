#include <gtest/gtest.h>

#include "alloc_counter.hpp"
#include "harness/config.hpp"
#include "harness/runner.hpp"
#include "link/trace_loader.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

// M9 hook: after the startup phase the simulation loop must not call operator new.
TEST(ZeroAlloc, SteadyStateLoopDoesNotAllocate) {
  for (const char* algo : {"reno", "cubic"}) {
    RunConfig cfg;
    cfg.algos = {algo};
    cfg.trace = "const12";
    cfg.buffer_pkts = 1000;
    cfg.duration = fromSec(10);
    const Trace trace = Trace::parse(bcc_test::constantTrace(12.0, 10000), "const12");
    Experiment e(cfg, trace);
    e.runUntil(fromSec(5));
    const std::uint64_t allocs0 = allocationCount();
    const std::uint64_t sent0 = e.packetsSent();
    e.runUntil(fromSec(10));
    EXPECT_GT(e.packetsSent() - sent0, 1000u) << algo;
    EXPECT_EQ(allocationCount() - allocs0, 0u) << algo;
  }
}
