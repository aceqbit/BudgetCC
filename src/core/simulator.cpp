#include "core/simulator.hpp"

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

bool Simulator::step(Time limit) {
  if (q_.empty() || q_.nextTime() >= limit) return false;
  Event e = q_.pop();
  now_ = e.time;
  hash_ = fnv(fnv(hash_, static_cast<std::uint64_t>(e.time)), e.seq);
  e.run();
  return true;
}

void Simulator::runUntil(Time end) {
  while (step(end)) {
  }
  if (end > now_) now_ = end;
}

}  // namespace bcc
