#include "core/packet_pool.hpp"

#include <stdexcept>

namespace bcc {

void PacketPool::addSlab() {
  slabs_.push_back(std::make_unique<Packet[]>(kSlabPackets));
  Packet* slab = slabs_.back().get();
  for (std::size_t i = 0; i < kSlabPackets; ++i) {
    slab[i].next_free = free_head_;
    free_head_ = &slab[i];
  }
  capacity_ += kSlabPackets;
  ++c_.slabs;
}

void PacketPool::reserve(std::size_t n) {
  slabs_.reserve(n / kSlabPackets + 2);
  while (capacity_ < n) addSlab();
}

Packet* PacketPool::alloc() {
  if (free_head_ == nullptr) addSlab();
  Packet* p = free_head_;
  free_head_ = p->next_free;
  *p = Packet{};
  p->in_use = true;
  ++c_.allocs;
  ++c_.in_use;
  if (c_.in_use > c_.peak_in_use) c_.peak_in_use = c_.in_use;
  return p;
}

void PacketPool::free(Packet* p) {
  if (!p->in_use) throw std::logic_error("PacketPool::free: packet is not allocated (double free)");
  p->in_use = false;
  p->next_free = free_head_;
  free_head_ = p;
  ++c_.frees;
  --c_.in_use;
}

}  // namespace bcc
