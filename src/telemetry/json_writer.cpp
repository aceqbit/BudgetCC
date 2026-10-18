#include "telemetry/json_writer.hpp"

#include <fmt/format.h>

#include <iterator>

namespace bcc {

void SampleJsonWriter::append(std::string& out, const Sample& s) {
  const std::size_t before = out.size();
  fmt::format_to(
      std::back_inserter(out),
      R"({{"t_ms":{},"run":"r{:04d}","rate_mbps":{:.1f},"cap_mbps":{:.1f},"q_ms":{:.1f},"owd_ms":{:.1f},"inflight_kb":{},"lost":{})",
      s.t_ms, s.run, s.rate_mbps, s.cap_mbps, s.q_ms, s.owd_ms, s.inflight_kb, s.lost);
  if (extras_) {
    fmt::format_to(std::back_inserter(out), R"(,"flow":{},"chat_mbps":{:.3f},"cwnd_kb":{:.1f},"qest_ms":{:.1f})", s.flow,
                   s.chat_mbps, s.cwnd_kb, s.qest_ms);
  }
  out.push_back('}');
  bytes_written_ += out.size() - before;
}

}  // namespace bcc
