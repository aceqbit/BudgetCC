#pragma once
// Telemetry path: simulation thread -> ring -> pusher thread -> JSON lines -> Loki HTTP push API.
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
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
  int flush_interval_ms = 1000;         // the pusher wakes at least this often
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

// The lines of one stream in a push batch, stored contiguously so a batch costs no per-line allocation.
struct StreamLines {
  Labels labels;
  std::vector<std::int64_t> ts;      // unix ns per line
  std::string text;                  // all lines back to back
  std::vector<std::uint32_t> ends;   // end offset of each line in `text`

  std::size_t size() const { return ts.size(); }
  std::string_view line(std::size_t i) const {
    const std::uint32_t begin = i == 0 ? 0 : ends[i - 1];
    return std::string_view(text).substr(begin, ends[i] - begin);
  }
  void add(std::int64_t t, std::string_view l) {
    ts.push_back(t);
    text.append(l);
    ends.push_back(static_cast<std::uint32_t>(text.size()));
  }
  // `write(std::string&)` appends one line to the shared text buffer.
  template <class F>
  void addWith(std::int64_t t, F&& write) {
    ts.push_back(t);
    write(text);
    ends.push_back(static_cast<std::uint32_t>(text.size()));
  }
  void clear() {
    ts.clear();
    text.clear();
    ends.clear();
  }
};

// Appends {"streams":[{"stream":{labels},"values":[["<ns>","<line>"],...]},...]} (empty streams are skipped).
void appendPushBody(std::string& out, const std::vector<StreamLines>& streams);

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
  void flushBatches(std::size_t& pending);
  void pushBatch(const std::vector<StreamLines>& batch, std::size_t lines);
  int post(const std::string& body);

  LokiConfig cfg_;
  std::int64_t base_ns_;
  SpscRing<Sample> ring_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::condition_variable flushed_;
  bool stop_ = false;
  bool flush_requested_ = false;
  std::vector<std::pair<Labels, Labels>> streams_;
  std::vector<SummaryLine> summaries_;
  TelemetryCounters c_;
  // Pusher-thread state, reused across drains so steady-state pushing does not allocate per line.
  SampleJsonWriter writer_{true};
  std::vector<StreamLines> lines_;  // slot 2*id: kind=sample, 2*id+1: kind=summary
  std::string body_;
  std::thread thread_;
};

}  // namespace bcc
