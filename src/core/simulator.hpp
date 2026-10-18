#pragma once
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "core/clock.hpp"
#include "core/event_queue.hpp"

namespace bcc {

// The only owner of simulated time.
class Simulator {
 public:
  Time now() const { return now_; }
  template <class F>
  void schedule(Time at, F fn) {
    if (at < now_) throw std::logic_error("Simulator::schedule: event in the past");
    q_.push(at, fn);
  }
  template <class F>
  void scheduleIn(Duration d, F fn) {
    schedule(now_ + d, fn);
  }
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
