#pragma once
#include <cstdint>
#include <functional>
#include <utility>

#include "core/clock.hpp"
#include "core/event_queue.hpp"

namespace bcc {

// The only owner of simulated time.
class Simulator {
 public:
  Time now() const { return now_; }
  void schedule(Time at, std::function<void()> fn);
  void scheduleIn(Duration d, std::function<void()> fn) { schedule(now_ + d, std::move(fn)); }
  // Runs the next event if its time is < limit. Returns false when nothing ran.
  bool step(Time limit);
  // Processes every event with time < end, then sets now() = end.
  void runUntil(Time end);
  std::uint64_t eventsProcessed() const { return q_.eventsProcessed(); }
  std::uint64_t traceHash() const { return hash_; }
  bool idle() const { return q_.empty(); }

 private:
  Time now_ = 0;
  EventQueue q_;
  std::uint64_t hash_ = 14695981039346656037ULL;  // FNV-1a over (time, seq) of executed events
};

}  // namespace bcc
