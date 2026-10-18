#pragma once
#include <cstdint>

#include "core/clock.hpp"

namespace bcc {

// What the receiver tells the sender about one delivered packet.
struct FeedbackItem {
  std::uint64_t seq;
  Time recv_ts;
};

}  // namespace bcc
