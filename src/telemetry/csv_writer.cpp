#include "telemetry/csv_writer.hpp"

#include <fmt/format.h>

#include <iterator>
#include <stdexcept>

namespace bcc {

std::string csvHeader() {
  return "t_ms,run,flow,rate_mbps,cap_mbps,q_ms,owd_ms,inflight_kb,lost,chat_mbps,cwnd_kb,qest_ms\n";
}

void appendCsvRow(std::string& out, const Sample& s) {
  fmt::format_to(std::back_inserter(out), "{},r{:04d},{},{},{},{},{},{},{},{},{},{}\n", s.t_ms, s.run, s.flow,
                 s.rate_mbps, s.cap_mbps, s.q_ms, s.owd_ms, s.inflight_kb, s.lost, s.chat_mbps, s.cwnd_kb, s.qest_ms);
}

CsvWriter::CsvWriter(const std::string& path) : f_(path, std::ios::binary) {
  if (!f_) throw std::runtime_error("cannot open csv file " + path);
  f_ << csvHeader();
}

void CsvWriter::write(const Sample& s) {
  buf_.clear();
  appendCsvRow(buf_, s);
  f_ << buf_;
}

void CsvWriter::close() { f_.close(); }

}  // namespace bcc
