#include "link/trace_loader.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace bcc {

namespace {
[[noreturn]] void fail(const std::string& name, std::size_t line, const std::string& msg) {
  throw std::runtime_error(name + ":" + std::to_string(line) + ": " + msg);
}
}  // namespace

Trace Trace::parse(const std::string& text, const std::string& name) {
  Trace t;
  t.name_ = name;
  std::istringstream in(text);
  std::string line;
  std::size_t lineno = 0;
  std::int64_t prev = -1;
  while (std::getline(in, line)) {
    ++lineno;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty()) continue;
    std::int64_t ms = 0;
    const char* b = line.data();
    const char* e = b + line.size();
    auto [p, ec] = std::from_chars(b, e, ms);
    if (ec != std::errc() || p != e) fail(name, lineno, "not a non-negative integer: '" + line + "'");
    if (ms < 0) fail(name, lineno, "negative timestamp");
    if (ms < prev) fail(name, lineno, "timestamps must be non-decreasing");
    if (ms == prev) {
      ++t.counts_.back();
    } else {
      t.times_.push_back(fromMs(ms));
      t.counts_.push_back(1);
    }
    prev = ms;
  }
  if (t.times_.empty()) throw std::runtime_error(name + ": empty trace");
  t.cum_.assign(t.times_.size() + 1, 0);
  for (std::size_t i = 0; i < t.times_.size(); ++i) t.cum_[i + 1] = t.cum_[i] + t.counts_[i];
  t.period_ = std::max<Duration>(t.times_.back(), fromMs(1));
  return t;
}

Trace Trace::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error(path + ": cannot open trace");
  std::ostringstream ss;
  ss << f.rdbuf();
  return parse(ss.str(), path);
}

std::uint64_t Trace::countBefore(Time rel) const {
  if (rel <= 0) return 0;
  const auto it = std::lower_bound(times_.begin(), times_.end(), rel);
  return cum_[static_cast<std::size_t>(it - times_.begin())];
}

std::uint64_t Trace::opportunities(Time t0, Time t1) const {
  t0 = std::max<Time>(t0, 0);
  if (t1 <= t0) return 0;
  std::int64_t k = t0 / period_;
  if (k > 0) --k;
  std::uint64_t total = 0;
  for (; k * period_ < t1; ++k) {
    const Time base = k * period_;
    total += countBefore(t1 - base) - countBefore(t0 - base);
  }
  return total;
}

Trace::Opp Trace::Cursor::next() {
  const Opp o{cycle_ * t_->period_ + t_->times_[idx_], t_->counts_[idx_]};
  if (++idx_ == t_->times_.size()) {
    idx_ = 0;
    ++cycle_;
  }
  return o;
}

}  // namespace bcc
