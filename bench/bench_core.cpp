#include "bench_core.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#include "alloc_counter.hpp"
#include "core/json.hpp"
#include "harness/config.hpp"
#include "harness/instrumented.hpp"
#include "harness/results.hpp"
#include "harness/runner.hpp"
#include "link/trace_loader.hpp"
#include "transport/sender.hpp"

#ifndef BUDGETCC_GIT_SHA
#define BUDGETCC_GIT_SHA "unknown"
#endif

namespace bcc {

namespace {
using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }

Trace loadBenchTrace(const BenchOptions& o) {
  const std::string name = o.trace.empty() ? realTraceNames().at(0) : o.trace;
  return Trace::load(resolveTracePath(name));
}

RunConfig benchConfig(const BenchOptions& o) {
  RunConfig c;
  c.algos = {o.algo};
  c.trace = o.trace.empty() ? realTraceNames().at(0) : o.trace;
  c.buffer_pkts = 1000;
  c.duration = o.duration;
  normalize(c);
  return c;
}
}  // namespace

double median(std::vector<double> v) {
  if (v.empty()) throw std::invalid_argument("median of nothing");
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double measureEventsPerSecond(const BenchOptions& o, bool telemetry) {
  const Trace trace = loadBenchTrace(o);
  const RunConfig cfg = benchConfig(o);
  std::vector<double> rates;
  for (int i = 0; i < o.repeats; ++i) {
    const auto t0 = Clock::now();
    std::uint64_t events = 0;
    if (telemetry) {
      LokiConfig lc;  // null sink: samples are formatted and counted, nothing leaves the process
      TelemetrySink sink(lc);
      events = runInstrumented(cfg, trace, &sink, nullptr).events;
    } else {
      events = runOnce(cfg, trace).events;
    }
    rates.push_back(static_cast<double>(events) / secondsSince(t0));
  }
  return median(rates);
}

double measureCpuPerAck(const BenchOptions& o) {
  constexpr std::uint64_t kBatch = 1000;  // packets in flight while a batch of feedback is replayed
  constexpr Duration kPacketGap = 1'250'000;  // 10 Mbps of 1250 B packets
  std::vector<double> ns_per_ack;
  for (int rep = 0; rep < o.repeats; ++rep) {
    Simulator sim;
    PacketPool pool;
    BulkSource source;
    CcConfig cc;
    Sender sender(sim, pool, 0, makeCongestionControl("budgetcc", cc), source, [](Packet*) {});
    std::uint64_t state = 12345;  // deterministic queueing-delay jitter, 0..39 ms
    double timed_ns = 0.0;
    std::uint64_t acks = 0;
    for (std::uint64_t first = 0; acks < o.ack_count; first += kBatch) {
      for (std::uint64_t i = 0; i < kBatch; ++i)
        sender.trackSent(first + i, static_cast<Time>(first + i) * kPacketGap, 1250);
      const auto t0 = Clock::now();
      for (std::uint64_t i = 0; i < kBatch; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const Time send_ts = static_cast<Time>(first + i) * kPacketGap;
        const Time recv_ts = send_ts + 20'000'000 + static_cast<Time>((state >> 33) % 40) * 1'000'000;
        sender.onFeedbackAt(recv_ts + 20'000'000, FeedbackItem{first + i, recv_ts});
      }
      timed_ns += std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
      acks += kBatch;
    }
    ns_per_ack.push_back(timed_ns / static_cast<double>(acks));
  }
  return median(ns_per_ack);
}

double measureAllocsPerPacket(const BenchOptions& o) {
  const Trace trace = loadBenchTrace(o);
  RunConfig cfg = benchConfig(o);
  cfg.duration = 10'000'000'000;
  Experiment e(cfg, trace);
  e.runUntil(5'000'000'000);
  const std::uint64_t allocs0 = allocationCount();
  const std::uint64_t sent0 = e.packetsSent();
  e.runUntil(10'000'000'000);
  const std::uint64_t pkts = e.packetsSent() - sent0;
  if (pkts == 0) throw std::runtime_error("no packets sent in the allocation window");
  return static_cast<double>(allocationCount() - allocs0) / static_cast<double>(pkts);
}

BenchResult runBench(const BenchOptions& o) {
  BenchResult r;
  r.events_per_s = measureEventsPerSecond(o, true);
  r.events_per_s_no_telemetry = measureEventsPerSecond(o, false);
  r.cpu_per_ack_ns = measureCpuPerAck(o);
  r.allocs_per_pkt = measureAllocsPerPacket(o);
  return r;
}

namespace {
JsonObject entry(const BenchResult& r, double matrix_runtime_s) {
  JsonObject o;
  o.num("events_per_s", r.events_per_s)
      .num("events_per_s_no_telemetry", r.events_per_s_no_telemetry)
      .num("cpu_per_ack_ns", r.cpu_per_ack_ns)
      .num("allocs_per_pkt", r.allocs_per_pkt)
      .num("matrix_runtime_s", matrix_runtime_s);
  return o;
}
}  // namespace

std::string benchJson(const std::string& phase, const BenchResult& r, double matrix_runtime_s, const std::string& sha) {
  JsonObject o;
  o.i64("schema", 1)
      .str("phase", phase)
      .str("git_sha", sha.empty() ? BUDGETCC_GIT_SHA : sha)
      .raw("env", environmentJson())
      .raw("bench", entry(r, matrix_runtime_s).finish());
  return o.finish();
}

std::string historyLine(const std::string& change, const BenchResult& r, double matrix_runtime_s, const std::string& sha) {
  JsonObject o;
  o.str("git_sha", sha.empty() ? BUDGETCC_GIT_SHA : sha)
      .str("change", change)
      .num("events_per_s", r.events_per_s)
      .num("events_per_s_no_telemetry", r.events_per_s_no_telemetry)
      .num("cpu_per_ack_ns", r.cpu_per_ack_ns)
      .num("allocs_per_pkt", r.allocs_per_pkt);
  if (matrix_runtime_s > 0.0) o.num("matrix_runtime_s", matrix_runtime_s);  // M10 only when a matrix was timed
  return o.finish();
}

}  // namespace bcc
