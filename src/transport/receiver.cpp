#include "transport/receiver.hpp"

#include <utility>

namespace bcc {

Receiver::Receiver(Simulator& sim, PacketPool& pool, std::uint32_t flow, Duration return_delay, FeedbackFn to_sender)
    : sim_(sim), pool_(pool), flow_(flow), return_delay_(return_delay), to_sender_(std::move(to_sender)) {}

void Receiver::onPacket(Packet* p) {
  RtpHeader h;
  if (!decodeRtp(p->hdr.data(), p->hdr.size(), h) || h.ssrc != flow_) {
    ++c_.bad_packets;
    pool_.free(p);
    return;
  }
  const Time now = sim_.now();
  const std::uint64_t seq = unwrap_.unwrap(h.seq);
  const Duration owd = now - p->send_ts;
  ++c_.recv_pkts;
  c_.recv_bytes += p->size;
  if (owd > fromMs(100)) ++c_.over_100ms;
  if (owd > fromMs(150)) ++c_.over_150ms;
  if (owd > fromMs(300)) ++c_.over_300ms;
  owd_ns_.push_back(owd);
  interval_.bytes += p->size;
  ++interval_.pkts;
  interval_.owd_ms_sum += toMs(owd);
  pool_.free(p);
  // PHASE1-NAIVE(A3): one feedback event (a heap-allocated std::function) per packet.
  const FeedbackItem fb{seq, now};
  sim_.scheduleIn(return_delay_, [this, fb] { to_sender_(fb); });
}

ReceiverInterval Receiver::takeInterval() {
  ReceiverInterval r = interval_;
  interval_ = ReceiverInterval{};
  return r;
}

}  // namespace bcc
