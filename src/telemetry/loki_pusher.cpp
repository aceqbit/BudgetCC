#include "telemetry/loki_pusher.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <iterator>

#include "core/json.hpp"

namespace bcc {

std::int64_t defaultBaseNs() {
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  return static_cast<std::int64_t>(now) - 60'000'000'000LL;
}

void appendPushBody(std::string& out, const std::vector<StreamLines>& streams) {
  out += "{\"streams\":[";
  bool first_stream = true;
  for (const auto& s : streams) {
    if (s.size() == 0) continue;
    if (!first_stream) out.push_back(',');
    first_stream = false;
    out += "{\"stream\":{";
    bool first_label = true;
    for (const auto& kv : s.labels) {
      if (!first_label) out.push_back(',');
      first_label = false;
      appendJsonString(out, kv.first);
      out.push_back(':');
      appendJsonString(out, kv.second);
    }
    out += "},\"values\":[";
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (i != 0) out.push_back(',');
      fmt::format_to(std::back_inserter(out), "[\"{}\",", s.ts[i]);
      appendJsonString(out, s.line(i));
      out.push_back(']');
    }
    out += "]}";
  }
  out += "]}";
}

TelemetrySink::TelemetrySink(LokiConfig cfg)
    : cfg_(std::move(cfg)),
      base_ns_(cfg_.base_ns != 0 ? cfg_.base_ns : defaultBaseNs()),
      ring_(cfg_.ring_capacity) {
  thread_ = std::thread([this] { loop(); });
}

TelemetrySink::~TelemetrySink() {
  {
    std::lock_guard<std::mutex> lk(m_);
    stop_ = true;
  }
  cv_.notify_all();
  thread_.join();
}

std::uint32_t TelemetrySink::addStream(Labels sample_labels, Labels summary_labels) {
  std::lock_guard<std::mutex> lk(m_);
  streams_.emplace_back(std::move(sample_labels), std::move(summary_labels));
  return static_cast<std::uint32_t>(streams_.size() - 1);
}

void TelemetrySink::summary(std::uint32_t stream, const std::string& json_line, std::int64_t t_ms) {
  std::lock_guard<std::mutex> lk(m_);
  summaries_.push_back(SummaryLine{stream, t_ms, json_line});
}

void TelemetrySink::flush() {
  std::unique_lock<std::mutex> lk(m_);
  flush_requested_ = true;
  cv_.notify_all();
  flushed_.wait(lk, [this] { return !flush_requested_ || stop_; });
}

TelemetryCounters TelemetrySink::counters() const {
  std::lock_guard<std::mutex> lk(m_);
  TelemetryCounters c = c_;
  c.dropped_samples = ring_.dropped();
  return c;
}

void TelemetrySink::loop() {
  std::unique_lock<std::mutex> lk(m_);
  for (;;) {
    cv_.wait_for(lk, std::chrono::milliseconds(cfg_.flush_interval_ms), [this] { return stop_ || flush_requested_; });
    const bool stopping = stop_;
    const bool flushing = flush_requested_;  // only a drain that started after the request may satisfy it
    lk.unlock();
    drain();
    lk.lock();
    if (flushing) {
      flush_requested_ = false;
      flushed_.notify_all();
    }
    if (stopping) return;
  }
}

void TelemetrySink::flushBatches(std::size_t& pending) {
  if (pending == 0) return;
  pushBatch(lines_, pending);
  pending = 0;
  for (auto& sl : lines_) sl.clear();  // keeps the buffers' capacity for the next batch
}

void TelemetrySink::drain() {
  std::vector<SummaryLine> summaries;
  {
    std::lock_guard<std::mutex> lk(m_);
    for (std::size_t i = lines_.size() / 2; i < streams_.size(); ++i) {
      lines_.push_back(StreamLines{streams_[i].first, {}, {}, {}});
      lines_.push_back(StreamLines{streams_[i].second, {}, {}, {}});
    }
    summaries.swap(summaries_);
  }
  std::size_t pending = 0;
  const std::uint64_t formatted_before = writer_.bytesWritten();
  Sample s;
  while (ring_.pop(s)) {
    lines_.at(2 * static_cast<std::size_t>(s.stream))
        .addWith(base_ns_ + static_cast<std::int64_t>(s.t_ms) * 1'000'000 + s.flow,
                 [&](std::string& text) { writer_.append(text, s); });
    if (++pending >= cfg_.batch_lines) flushBatches(pending);
  }
  // Summaries live in their own streams (kind=summary); keep them out of sample batches.
  flushBatches(pending);
  for (const auto& sm : summaries) {
    lines_.at(2 * static_cast<std::size_t>(sm.stream) + 1).add(base_ns_ + sm.t_ms * 1'000'000, sm.line);
    ++pending;
  }
  flushBatches(pending);
  std::lock_guard<std::mutex> lk(m_);
  c_.bytes_written += writer_.bytesWritten() - formatted_before;
}

namespace {
// A numeric host needs no name resolution. httplib resolves names through glibc's getaddrinfo_a when a
// connection timeout is set, and that helper thread is invisible to TSan's allocator (it segfaults).
bool isNumericHost(const std::string& url) {
  const auto scheme = url.find("://");
  const std::string host = url.substr(scheme == std::string::npos ? 0 : scheme + 3);
  const std::string name = host.substr(0, host.find_first_of(":/"));
  return !name.empty() && name.find_first_not_of("0123456789.") == std::string::npos;
}
}  // namespace

int TelemetrySink::post(const std::string& body) {
  httplib::Client cli(cfg_.url);
  // httplib's default connection timeout (300 s) is positive, so only an explicit 0 avoids getaddrinfo_a.
  if (isNumericHost(cfg_.url)) {
    cli.set_connection_timeout(0, 0);
  } else {
    cli.set_connection_timeout(cfg_.connect_timeout_ms / 1000, (cfg_.connect_timeout_ms % 1000) * 1000);
  }
  cli.set_read_timeout(10, 0);
  cli.set_write_timeout(10, 0);
  const auto res = cli.Post("/loki/api/v1/push", body, "application/json");
  return res ? res->status : -1;
}

void TelemetrySink::pushBatch(const std::vector<StreamLines>& batch, std::size_t lines) {
  if (cfg_.url.empty()) {  // null sink: lines were formatted, nothing leaves the process
    std::lock_guard<std::mutex> lk(m_);
    c_.pushed_lines += lines;
    ++c_.batches;
    return;
  }
  body_.clear();
  appendPushBody(body_, batch);
  const std::string& body = body_;
  const auto t0 = std::chrono::steady_clock::now();
  bool ok = false;
  int backoff = cfg_.backoff_ms;
  for (int attempt = 0; attempt < cfg_.max_attempts && !ok; ++attempt) {
    const int status = post(body);
    if (status >= 200 && status < 300) {
      ok = true;
    } else if (status == 429 || status >= 500 || status < 0) {
      if (attempt + 1 < cfg_.max_attempts) std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
      backoff *= 2;
    } else {
      break;  // other 4xx: retrying cannot help
    }
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::lock_guard<std::mutex> lk(m_);
  c_.push_ms += ms;
  ++c_.batches;
  if (ok) {
    c_.pushed_lines += lines;
  } else {
    ++c_.push_failures;
  }
}

}  // namespace bcc
