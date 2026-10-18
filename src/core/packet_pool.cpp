#include "core/packet_pool.hpp"

namespace bcc {

PacketPool::~PacketPool() {
  while (live_head_ != nullptr) {
    Packet* next = live_head_->live_next;
    delete live_head_;
    live_head_ = next;
  }
}

Packet* PacketPool::alloc() {
  Packet* p = new Packet();
  p->live_next = live_head_;
  if (live_head_ != nullptr) live_head_->live_prev = p;
  live_head_ = p;
  ++c_.allocs;
  ++c_.in_use;
  if (c_.in_use > c_.peak_in_use) c_.peak_in_use = c_.in_use;
  return p;
}

void PacketPool::free(Packet* p) {
  if (p->live_prev != nullptr) {
    p->live_prev->live_next = p->live_next;
  } else {
    live_head_ = p->live_next;
  }
  if (p->live_next != nullptr) p->live_next->live_prev = p->live_prev;
  delete p;
  ++c_.frees;
  --c_.in_use;
}

}  // namespace bcc
