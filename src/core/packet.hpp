#pragma once
// One datagram on the wire (1250 B: 12 B RTP-style header + payload).
#include <array>
#include <cstdint>

#include "core/clock.hpp"

namespace bcc {

constexpr std::uint32_t kWireBytes = 1250;
constexpr std::uint32_t kPayloadBytes = 1200;
constexpr std::uint32_t kOpportunityBytes = 1500;

struct Packet {
  std::array<std::uint8_t, 12> hdr{};
  std::uint32_t flow = 0;  // SSRC / flow id
  std::uint64_t seq = 0;
  std::uint32_t size = kWireBytes;
  Time send_ts = 0;
  Time enq_ts = 0;
  Packet* next_free = nullptr;  // pool free list
  bool in_use = false;
};

}  // namespace bcc
