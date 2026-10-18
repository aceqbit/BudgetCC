#pragma once
// Phase 1 event queue, kept as the oracle for the differential tests (E1, E2).
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

#include "core/clock.hpp"

namespace bccref {

class NaiveEventQueue {
 public:
  struct Event {
    bcc::Time time;
    std::uint64_t seq;
    std::function<void()> fn;
  };

  void push(bcc::Time t, std::function<void()> fn) { q_.push(Event{t, next_seq_++, std::move(fn)}); }
  bool empty() const { return q_.empty(); }
  std::size_t size() const { return q_.size(); }
  bcc::Time nextTime() const { return q_.top().time; }
  Event pop() {
    Event e = q_.top();
    q_.pop();
    return e;
  }

 private:
  struct Later {
    bool operator()(const Event& a, const Event& b) const {
      return a.time != b.time ? a.time > b.time : a.seq > b.seq;
    }
  };
  std::priority_queue<Event, std::vector<Event>, Later> q_;
  std::uint64_t next_seq_ = 0;
};

}  // namespace bccref
