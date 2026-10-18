#pragma once
// In-flight packet table indexed by sequence number (E1: replaces std::map<seq, Sent>).
// Live sequence numbers lie in [lo, hi]; slot = seq & (capacity - 1). The table doubles when the span
// hi - lo + 1 exceeds its capacity, which only happens when more packets are outstanding than ever before.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "core/clock.hpp"

namespace bcc {

struct Sent {
  Time send_ts = 0;
  std::uint32_t size = 0;
  int later_acked = 0;
};

class InflightRing {
 public:
  explicit InflightRing(std::size_t initial_capacity = 4096) {
    std::size_t c = 2;
    while (c < initial_capacity) c <<= 1;
    slots_.resize(c);
  }

  bool empty() const { return live_ == 0; }
  std::size_t size() const { return live_; }
  std::size_t capacity() const { return slots_.size(); }

  // Registers seq. Requires seq to be above every live sequence below it being reused, i.e. seq >= oldest live
  // sequence (or the table empty), and seq not already live.
  void insert(std::uint64_t seq, const Sent& s) {
    if (live_ == 0) {
      lo_ = hi_ = seq;
    } else {
      if (seq < lo_) throw std::logic_error("InflightRing::insert: sequence below the oldest live one");
      while (seq - lo_ >= slots_.size()) grow();
      if (seq > hi_) hi_ = seq;
    }
    Slot& sl = slot(seq);
    if (sl.live) throw std::logic_error("InflightRing::insert: sequence already live");
    sl.live = true;
    sl.v = s;
    ++live_;
  }

  Sent* find(std::uint64_t seq) {
    if (live_ == 0 || seq < lo_ || seq > hi_) return nullptr;
    Slot& sl = slot(seq);
    return sl.live ? &sl.v : nullptr;
  }

  void erase(std::uint64_t seq) {
    if (find(seq) == nullptr) return;
    slot(seq).live = false;
    --live_;
    while (live_ != 0 && !slot(lo_).live) ++lo_;
  }

  std::uint64_t oldestSeq() const { return lo_; }
  const Sent& oldest() const { return slots_[lo_ & (slots_.size() - 1)].v; }

  // Calls f(seq, Sent&) for every live sequence below `limit`, ascending. f may erase the visited sequence.
  template <class F>
  void forEachBelow(std::uint64_t limit, F f) {
    if (live_ == 0) return;
    const std::uint64_t end = limit < hi_ + 1 ? limit : hi_ + 1;
    for (std::uint64_t s = lo_; s < end; ++s) {
      Slot& sl = slot(s);
      if (sl.live) f(s, sl.v);
    }
  }

 private:
  struct Slot {
    Sent v;
    bool live = false;
  };

  Slot& slot(std::uint64_t seq) { return slots_[seq & (slots_.size() - 1)]; }

  void grow() {
    std::vector<Slot> bigger(slots_.size() * 2);
    for (std::uint64_t s = lo_; s <= hi_; ++s) {
      const Slot& old = slots_[s & (slots_.size() - 1)];
      if (old.live) bigger[s & (bigger.size() - 1)] = old;
    }
    slots_ = std::move(bigger);
  }

  std::vector<Slot> slots_;
  std::uint64_t lo_ = 0;
  std::uint64_t hi_ = 0;
  std::size_t live_ = 0;
};

}  // namespace bcc
