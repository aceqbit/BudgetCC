#include "harness/summary.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bcc {

std::int64_t percentileNearestRank(const std::vector<std::int64_t>& sorted, int pct) {
  const std::size_t n = sorted.size();
  std::size_t rank = (static_cast<std::size_t>(pct) * n + 99) / 100;  // ceil(pct * n / 100)
  rank = std::min(std::max<std::size_t>(rank, 1), n);
  return sorted[rank - 1];
}

double jainIndex(const std::vector<double>& x) {
  double s = 0.0, s2 = 0.0;
  for (double v : x) {
    s += v;
    s2 += v * v;
  }
  if (s2 == 0.0) return 1.0;
  return s * s / (static_cast<double>(x.size()) * s2);
}

Recovery recoveryStats(const Trace& trace, const std::vector<double>& q_ms, Duration duration, double qstar_ms) {
  constexpr Duration kStep = 10'000'000;
  // 1. Capacity-drop events: mean capacity over [t, t+200ms) <= 50% of the mean over [t-1s, t).
  std::vector<Time> events;
  Time last_trigger = 0;
  bool have_trigger = false;
  for (Time t = kStep * 100; t + 200'000'000 <= duration; t += kStep) {
    const double before = static_cast<double>(trace.capacityBytes(t - 1'000'000'000, t)) / 1.0;
    const double after = static_cast<double>(trace.capacityBytes(t, t + 200'000'000)) / 0.2;
    if (before <= 0.0 || after > 0.5 * before) continue;
    if (!have_trigger || t - last_trigger >= 2'000'000'000) events.push_back(t);  // merge triggers < 2 s apart
    last_trigger = t;
    have_trigger = true;
  }
  Recovery r;
  r.events = static_cast<std::uint32_t>(events.size());
  if (events.empty()) return r;

  // 2. Per event: time until the queue delay, once above Q*, stays <= Q* for 100 ms.
  std::vector<double> values;
  const std::size_t n = q_ms.size();
  for (std::size_t i = 0; i < events.size(); ++i) {
    const Time te = events[i];
    const Time bound = i + 1 < events.size() ? events[i + 1] : duration;
    std::size_t k = static_cast<std::size_t>(te / kStep);  // sample k is taken at t = (k + 1) * 10 ms
    while (k < n && q_ms[k] <= qstar_ms) ++k;
    if (k >= n) {  // the queue never exceeded Q* after the event
      values.push_back(0.0);
      continue;
    }
    bool recovered = false;
    for (std::size_t s = k + 1; s + 10 <= n; ++s) {
      const Time ts = static_cast<Time>(s + 1) * kStep;
      if (ts >= bound) break;
      bool ok = true;
      for (std::size_t j = s; j < s + 10 && ok; ++j) ok = q_ms[j] <= qstar_ms;
      if (ok) {
        values.push_back(static_cast<double>(ts - te) / 1e6);
        recovered = true;
        break;
      }
    }
    if (!recovered) {
      ++r.censored;
      values.push_back(static_cast<double>(bound - te) / 1e6);
    }
  }
  std::sort(values.begin(), values.end());
  const std::size_t m = values.size();
  r.median_ms = m % 2 == 1 ? values[m / 2] : 0.5 * (values[m / 2 - 1] + values[m / 2]);
  return r;
}

Summary computeSummary(const SummaryInput& in) {
  Summary s;
  std::uint64_t bytes = 0;
  for (std::uint64_t b : in.flow_bytes) bytes += b;
  s.delivered_bytes = bytes;
  const double cap = static_cast<double>(in.trace->capacityBytes(0, in.duration));
  s.utilisation_pct = cap > 0.0 ? 100.0 * static_cast<double>(bytes) / cap : std::numeric_limits<double>::quiet_NaN();

  std::vector<std::int64_t> all;
  for (const auto* v : in.owd_ns) all.insert(all.end(), v->begin(), v->end());
  std::sort(all.begin(), all.end());
  const double nan = std::numeric_limits<double>::quiet_NaN();
  if (all.empty()) {
    s.p95_owd_ms = s.p99_owd_ms = s.violation_pct = s.violation_pct_d100 = s.violation_pct_d150 =
        s.violation_pct_d300 = nan;
  } else {
    s.p95_owd_ms = static_cast<double>(percentileNearestRank(all, 95)) / 1e6;
    s.p99_owd_ms = static_cast<double>(percentileNearestRank(all, 99)) / 1e6;
    auto over = [&all](double d_ms) {
      const auto limit = static_cast<std::int64_t>(std::llround(d_ms * 1e6));
      const auto it = std::upper_bound(all.begin(), all.end(), limit);
      return 100.0 * static_cast<double>(all.end() - it) / static_cast<double>(all.size());
    };
    s.violation_pct = over(in.budget_ms);
    s.violation_pct_d100 = over(100.0);
    s.violation_pct_d150 = over(150.0);
    s.violation_pct_d300 = over(300.0);
  }
  if (in.q_ms != nullptr) s.recovery = recoveryStats(*in.trace, *in.q_ms, in.duration, in.qstar_ms);
  if (in.flow_bytes.size() >= 2) {
    std::vector<double> x;
    x.reserve(in.flow_bytes.size());
    for (const auto b : in.flow_bytes) x.push_back(static_cast<double>(b));
    s.jain = jainIndex(x);
  }
  return s;
}

}  // namespace bcc
