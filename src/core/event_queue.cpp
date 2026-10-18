#include "core/event_queue.hpp"

#include <utility>

namespace bcc {

void EventQueue::push(Time t, std::function<void()> fn) { q_.push(Event{t, next_seq_++, std::move(fn)}); }

EventQueue::Event EventQueue::pop() {
  Event e = q_.top();  // PHASE1-NAIVE(E1): copies the std::function
  q_.pop();
  ++popped_;
  return e;
}

}  // namespace bcc
