#include "core/simulator.hpp"

#include <stdexcept>
#include <utility>

namespace bcc {

namespace {
std::uint64_t fnv(std::uint64_t h, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    h ^= (v >> (8 * i)) & 0xffU;
    h *= 1099511628211ULL;
  }
  return h;
}
}  // namespace

void Simulator::schedule(Time at, std::function<void()> fn) {
  if (at < now_) throw std::logic_error("Simulator::schedule: event in the past");
  q_.push(at, std::move(fn));
}

bool Simulator::step(Time limit) {
  if (q_.empty() || q_.nextTime() >= limit) return false;
  EventQueue::Event e = q_.pop();
  now_ = e.time;
  hash_ = fnv(fnv(hash_, static_cast<std::uint64_t>(e.time)), e.seq);
  e.fn();
  return true;
}

void Simulator::runUntil(Time end) {
  while (step(end)) {
  }
  if (end > now_) now_ = end;
}

}  // namespace bcc
