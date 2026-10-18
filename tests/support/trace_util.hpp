#pragma once
// Builds Mahimahi-format trace text for tests. 12 Mbps = 1 opportunity (1500 B) per millisecond.
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace bcc_test {

// Segments: (start_ms, mbps). The rate holds until the next segment starts.
inline std::string rateTrace(const std::vector<std::pair<int, double>>& segs, int total_ms) {
  std::string out;
  double acc = 0.0;
  for (int ms = 1; ms <= total_ms; ++ms) {
    double mbps = segs.front().second;
    for (const auto& s : segs)
      if (ms > s.first) mbps = s.second;
    acc += mbps / 12.0;
    int n = static_cast<int>(std::floor(acc + 1e-9));
    acc -= n;
    for (int i = 0; i < n; ++i) out += std::to_string(ms) + "\n";
  }
  return out;
}

inline std::string constantTrace(double mbps, int total_ms) { return rateTrace({{0, mbps}}, total_ms); }

}  // namespace bcc_test
