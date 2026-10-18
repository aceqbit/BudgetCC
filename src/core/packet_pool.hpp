#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/packet.hpp"

namespace bcc {

struct PoolCounters {
  std::uint64_t allocs = 0;
  std::uint64_t frees = 0;
  std::uint64_t in_use = 0;
  std::uint64_t peak_in_use = 0;
  std::uint64_t slabs = 0;  // chunks obtained from the heap; stays constant in steady state
};

// Slab-backed free list: after reserve() (or warm-up) alloc/free never touch the heap (E1).
class PacketPool {
 public:
  static constexpr std::size_t kSlabPackets = 1024;

  PacketPool() = default;
  PacketPool(const PacketPool&) = delete;
  PacketPool& operator=(const PacketPool&) = delete;

  // Ensures at least n packets are available without further heap use.
  void reserve(std::size_t n);
  Packet* alloc();
  void free(Packet* p);
  const PoolCounters& counters() const { return c_; }

 private:
  void addSlab();

  std::vector<std::unique_ptr<Packet[]>> slabs_;
  Packet* free_head_ = nullptr;
  std::size_t capacity_ = 0;
  PoolCounters c_;
};

}  // namespace bcc
