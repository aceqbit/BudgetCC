#pragma once
#include <cstdint>

#include "core/packet.hpp"

namespace bcc {

struct PoolCounters {
  std::uint64_t allocs = 0;
  std::uint64_t frees = 0;
  std::uint64_t in_use = 0;
  std::uint64_t peak_in_use = 0;
};

// PHASE1-NAIVE(E1): every packet is a heap allocation.
class PacketPool {
 public:
  PacketPool() = default;
  PacketPool(const PacketPool&) = delete;
  PacketPool& operator=(const PacketPool&) = delete;
  ~PacketPool();

  Packet* alloc();
  void free(Packet* p);
  const PoolCounters& counters() const { return c_; }

 private:
  Packet* live_head_ = nullptr;
  PoolCounters c_;
};

}  // namespace bcc
