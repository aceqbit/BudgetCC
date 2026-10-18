#pragma once
// Phase 1 sample formatter (fresh std::string per sample), the oracle for the E4 byte-equality test.
#include <fmt/format.h>

#include <string>

#include "telemetry/sample.hpp"

namespace bccref {

class NaiveSampleJsonWriter {
 public:
  explicit NaiveSampleJsonWriter(bool extras = true) : extras_(extras) {}
  std::string write(const bcc::Sample& s) {
    std::string out = fmt::format(
        R"({{"t_ms":{},"run":"r{:04d}","rate_mbps":{:.1f},"cap_mbps":{:.1f},"q_ms":{:.1f},"owd_ms":{:.1f},"inflight_kb":{},"lost":{})",
        s.t_ms, s.run, s.rate_mbps, s.cap_mbps, s.q_ms, s.owd_ms, s.inflight_kb, s.lost);
    if (extras_) {
      out += fmt::format(R"(,"flow":{},"chat_mbps":{:.3f},"cwnd_kb":{:.1f},"qest_ms":{:.1f})", s.flow, s.chat_mbps,
                         s.cwnd_kb, s.qest_ms);
    }
    out.push_back('}');
    return out;
  }

 private:
  bool extras_;
};

}  // namespace bccref
