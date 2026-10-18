#pragma once
#include <cstdint>
#include <string>

#include "telemetry/sample.hpp"

namespace bcc {

// Formats a Sample as one JSON line:
//   {"t_ms":1230,"run":"r0042","rate_mbps":8.4,"cap_mbps":9.1,"q_ms":42.0,"owd_ms":71.5,"inflight_kb":96,"lost":0}
// With `extras` the optional fields flow, chat_mbps, cwnd_kb and qest_ms follow.
class SampleJsonWriter {
 public:
  explicit SampleJsonWriter(bool extras = true) : extras_(extras) {}
  // PHASE1-NAIVE(E4): builds a fresh std::string per sample.
  std::string write(const Sample& s);
  std::uint64_t bytesWritten() const { return bytes_written_; }

 private:
  bool extras_;
  std::uint64_t bytes_written_ = 0;
};

}  // namespace bcc
