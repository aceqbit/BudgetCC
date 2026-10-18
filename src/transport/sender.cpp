#include "transport/sender.hpp"

#include <algorithm>
#include <utility>

#include "transport/rtp_header.hpp"

namespace bcc {

Sender::Sender(Simulator& sim, PacketPool& pool, std::uint32_t flow, std::unique_ptr<CongestionControl> cc,
               TrafficSource& source, TxFn tx)
    : sim_(sim),
      pool_(pool),
      flow_(flow),
      cc_(std::move(cc)),
      source_(source),
      tx_(std::move(tx)),
      pacer_(cc_->pacingRateBps()) {}

void Sender::start(Time at) {
  sim_.schedule(at, [this, at] {
    started_ = true;
    sim_.schedule(at + kControlInterval, [this] { onControlTick(sim_.now()); });
    trySend();
  });
}

void Sender::onControlTick(Time now) {
  cc_->onTimer(now);
  pacer_.setRate(cc_->pacingRateBps());
  while (!inflight_.empty() && now - inflight_.oldest().send_ts > lossTimeout())
    declareLost(inflight_.oldestSeq(), now, true);
  trySend();
  sim_.schedule(now + kControlInterval, [this] { onControlTick(sim_.now()); });
}

Duration Sender::lossTimeout() const { return std::max<Duration>(200'000'000, 2 * srtt_); }

void Sender::trackSent(std::uint64_t seq, Time send_ts, std::uint32_t size) {
  inflight_.insert(seq, Sent{send_ts, size, 0});
  inflight_bytes_ += size;
  next_seq_ = std::max(next_seq_, seq + 1);
  ++c_.sent_pkts;
  c_.sent_bytes += size;
}

void Sender::trySend() {
  if (!started_) return;
  const Time now = sim_.now();
  while (source_.hasPacket(now)) {
    const std::uint32_t cap = cc_->inflightCapBytes();
    if (inflight_bytes_ != 0 && inflight_bytes_ + kWireBytes > cap) return;  // an ACK will unblock us
    if (!pacer_.ready(now)) {
      armSendTimer(pacer_.nextSend());
      return;
    }
    sendOne(now);
  }
}

void Sender::armSendTimer(Time at) {
  if (timer_armed_ && timer_at_ == at) return;
  timer_armed_ = true;
  timer_at_ = at;
  const std::uint64_t gen = ++timer_gen_;
  sim_.schedule(at, [this, gen] {
    if (gen != timer_gen_) return;
    timer_armed_ = false;
    trySend();
  });
}

void Sender::sendOne(Time now) {
  Packet* p = pool_.alloc();
  p->flow = flow_;
  p->seq = next_seq_;
  p->size = kWireBytes;
  p->send_ts = now;
  RtpHeader h;
  h.seq = static_cast<std::uint16_t>(next_seq_);
  h.timestamp = static_cast<std::uint32_t>(now / 11'111);  // 90 kHz media clock
  h.ssrc = flow_;
  encodeRtp(h, p->hdr.data());
  trackSent(next_seq_, now, kWireBytes);
  pacer_.onSend(now, kWireBytes);
  source_.consume();
  tx_(p);
}

void Sender::declareLost(std::uint64_t seq, Time now, bool timeout) {
  const LossInfo l{now, seq, inflight_.find(seq)->size, timeout};
  inflight_bytes_ -= l.bytes;
  inflight_.erase(seq);
  ++c_.lost_pkts;
  if (timeout) ++c_.timeouts;
  cc_->onLoss(l);
}

void Sender::onFeedbackAt(Time now, const FeedbackItem& fb) {
  const Sent* found = inflight_.find(fb.seq);
  if (found == nullptr) return;  // already declared lost
  const Sent s = *found;
  inflight_bytes_ -= s.size;
  inflight_.erase(fb.seq);
  ++c_.acked_pkts;
  const Duration rtt = now - s.send_ts;
  srtt_ = srtt_ == 0 ? rtt : srtt_ + (rtt - srtt_) / 8;
  cc_->onAck(AckInfo{now, s.send_ts, fb.recv_ts, s.size, fb.seq, rtt, fb.recv_ts - s.send_ts});

  inflight_.forEachBelow(fb.seq, [&](std::uint64_t seq, Sent& older) {
    if (++older.later_acked >= kReorderThreshold) declareLost(seq, now, false);
  });
  trySend();
}

}  // namespace bcc
