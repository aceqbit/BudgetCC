#include <gtest/gtest.h>

#include "alloc_counter.hpp"
#include "bench_core.hpp"
#include "core/json.hpp"

using namespace bcc;

namespace {
BenchOptions smoke() {
  BenchOptions o;
  o.trace = "sawtooth";
  o.duration = 1'000'000'000;
  o.repeats = 1;
  o.ack_count = 5000;
  return o;
}
}  // namespace

TEST(Bench, MedianOddEvenAndEmpty) {
  EXPECT_DOUBLE_EQ(median({3.0, 1.0, 2.0}), 2.0);
  EXPECT_DOUBLE_EQ(median({4.0, 1.0, 2.0, 3.0}), 2.5);
  EXPECT_THROW(median({}), std::invalid_argument);
}

TEST(Bench, AllocationHookCountsOperatorNew) {
  const std::uint64_t before = allocationCount();
  int* volatile p = new int(7);  // volatile: the compiler may not elide the new/delete pair
  EXPECT_EQ(allocationCount(), before + 1);
  delete p;
  char* volatile a = new char[16];
  EXPECT_EQ(allocationCount(), before + 2);
  delete[] a;
}

TEST(Bench, OneSecondRunProducesAllFourMetrics) {
  const BenchResult r = runBench(smoke());
  EXPECT_GT(r.events_per_s, 0.0);
  EXPECT_GT(r.events_per_s_no_telemetry, 0.0);
  EXPECT_GT(r.cpu_per_ack_ns, 0.0);
  EXPECT_GE(r.allocs_per_pkt, 0.0);
}

TEST(Bench, JsonCarriesSchemaEnvAndEntryFields) {
  BenchResult r;
  r.events_per_s = 1.5;
  r.events_per_s_no_telemetry = 2.5;
  r.cpu_per_ack_ns = 300.0;
  r.allocs_per_pkt = 4.0;
  const std::string j = benchJson("p1", r, 12.5);
  for (const char* k : {"\"schema\":1", "\"phase\":\"p1\"", "\"git_sha\"", "\"env\":{", "\"cpu\"", "\"power_plan\"",
                        "\"events_per_s\":1.5", "\"cpu_per_ack_ns\":300", "\"allocs_per_pkt\":4", "\"matrix_runtime_s\":12.5"})
    EXPECT_NE(j.find(k), std::string::npos) << k;
  const std::string h = historyLine("E1", r, 12.5);
  EXPECT_NE(h.find("\"change\":\"E1\""), std::string::npos);
  EXPECT_NE(h.find("\"git_sha\""), std::string::npos);
}
