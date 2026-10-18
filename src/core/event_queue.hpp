#pragma once
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

#include "core/clock.hpp"

namespace bcc {

// PHASE1-NAIVE(E1,E2): std::priority_queue of std::function events.
class EventQueue {
 public:
  struct Event {
    Time time;
    std::uint64_t seq;
    std::function<void()> fn;
  };

  void push(Time t, std::function<void()> fn);
  bool empty() const { return q_.empty(); }
  std::size_t size() const { return q_.size(); }
  Time nextTime() const { return q_.top().time; }
  Event pop();
  std::uint64_t eventsProcessed() const { return popped_; }

 private:
  struct Later {
    bool operator()(const Event& a, const Event& b) const {
      return a.time != b.time ? a.time > b.time : a.seq > b.seq;
    }
  };
  std::priority_queue<Event, std::vector<Event>, Later> q_;
  std::uint64_t next_seq_ = 0;
  std::uint64_t popped_ = 0;
};

}  // namespace bcc
