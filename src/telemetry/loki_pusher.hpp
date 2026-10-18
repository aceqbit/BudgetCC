#pragma once
// Telemetry path: simulation thread -> ring -> pusher thread -> JSON lines -> Loki HTTP push API.
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/ring_buffer.hpp"
#include "telemetry/json_writer.hpp"
#include "telemetry/sample.hpp"

namespace bcc {

using Labels = std::map<std::string, std::string>;

struct LokiConfig {
  std::string url;                      // "http://localhost:3100"; empty = format and count, send nothing
  std::size_t batch_lines = 2000;       // lines per push request (1000-5000)
  int max_attempts = 4;
  int backoff_ms = 100;                 // doubled after every failed attempt
  int connect_timeout_ms = 2000;
  int flush_interval_ms = 1000;         // PHASE1-NAIVE(E4): the pusher wakes once per second
  std::int64_t base_ns = 0;             // unix-epoch ns of simulated t = 0; 0 = now - 60 s
  std::size_t ring_capacity = 65536;
};

struct TelemetryCounters {
  std::uint64_t pushed_lines = 0;
  std::uint64_t push_failures = 0;   // batches given up on
  std::uint64_t batches = 0;
  double push_ms = 0.0;              // wall time spent in HTTP pushes
  std::uint64_t bytes_written = 0;   // JSON bytes formatted
  std::uint64_t dropped_samples = 0; // ring overflow
};

// Unix-epoch nanoseconds "now - longest trace length", so that base + simulated time never lies in the future.
std::int64_t defaultBaseNs();

// {"streams":[{"stream":{labels},"values":[["<ns>","<line>"],...]},...]}
struct StreamLines {
  Labels labels;
  std::vector<std::pair<std::int64_t, std::string>> lines;  // (unix ns, JSON line)
};
std::string buildPushBody(const std::vector<StreamLines>& streams);

class TelemetrySink {
 public:
  explicit TelemetrySink(LokiConfig cfg);
  ~TelemetrySink();
  TelemetrySink(const TelemetrySink&) = delete;
  TelemetrySink& operator=(const TelemetrySink&) = delete;

  // Registers the label sets of one run (kind=sample and kind=summary) and returns its stream id.
  std::uint32_t addStream(Labels sample_labels, Labels summary_labels);
  // Simulation thread. Never blocks; a full ring drops the sample and counts it.
  void sample(const Sample& s) { ring_.push(s); }
  // One summary JSON line for a run, stamped at base + t_ms.
  void summary(std::uint32_t stream, const std::string& json_line, std::int64_t t_ms);
  // Blocks until everything queued so far has been pushed (or given up on).
  void flush();
  TelemetryCounters counters() const;

 private:
  struct SummaryLine {
    std::uint32_t stream;
    std::int64_t t_ms;
    std::string line;
  };
  void loop();
  void drain();
  void pushBatch(std::vector<StreamLines>& batch, std::size_t lines);
  int post(const std::string& body);

  LokiConfig cfg_;
  std::int64_t base_ns_;
  MutexRing<Sample> ring_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::condition_variable flushed_;
  bool stop_ = false;
  bool flush_requested_ = false;
  std::vector<std::pair<Labels, Labels>> streams_;
  std::vector<SummaryLine> summaries_;
  TelemetryCounters c_;
  SampleJsonWriter writer_{true};
  std::thread thread_;
};

}  // namespace bcc
