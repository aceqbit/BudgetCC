#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "cc/cc_interface.hpp"
#include "core/packet_pool.hpp"
#include "core/simulator.hpp"
#include "transport/feedback.hpp"
#include "transport/inflight_ring.hpp"
#include "transport/pacer.hpp"
#include "transport/rtp_header.hpp"
#include "transport/traffic_source.hpp"

namespace bcc {

struct SenderCounters {
  std::uint64_t sent_pkts = 0;
  std::uint64_t sent_bytes = 0;
  std::uint64_t acked_pkts = 0;
  std::uint64_t lost_pkts = 0;
  std::uint64_t timeouts = 0;
};

// What the harness needs from a flow's sender regardless of the controller type (cold paths only).
class SenderBase {
 public:
  virtual ~SenderBase() = default;
  virtual void start(Time at) = 0;
  virtual void kick() = 0;  // new application data became available (video frame)
  virtual void onFeedback(const FeedbackItem& fb) = 0;
  virtual std::uint64_t inflightBytes() const = 0;
  virtual const SenderCounters& counters() const = 0;
  virtual CongestionControl& cc() = 0;
  virtual const CongestionControl& cc() const = 0;
};

// One flow (flow id = RTP SSRC): pacer + in-flight tracking + loss detection around a controller.
// CC is either the abstract CongestionControl (virtual calls; tests and generic tools) or a final concrete
// controller, in which case every per-packet call into the controller is resolved at compile time (E5).
template <class CC>
class BasicSender final : public SenderBase {
  static_assert(std::is_base_of<CongestionControl, CC>::value, "CC must be a CongestionControl");

 public:
  using TxFn = std::function<void(Packet*)>;
  static constexpr Duration kControlInterval = 10'000'000;  // 10 ms
  static constexpr int kReorderThreshold = 3;

  BasicSender(Simulator& sim, PacketPool& pool, std::uint32_t flow, std::unique_ptr<CC> cc, TrafficSource& source,
              TxFn tx)
      : sim_(sim),
        pool_(pool),
        flow_(flow),
        cc_(std::move(cc)),
        source_(source),
        tx_(std::move(tx)),
        pacer_(cc_->pacingRateBps()) {}

  void start(Time at) override {
    sim_.schedule(at, [this, at] {
      started_ = true;
      sim_.schedule(at + kControlInterval, [this] { onControlTick(sim_.now()); });
      trySend();
    });
  }
  void kick() override { trySend(); }
  void onFeedback(const FeedbackItem& fb) override { onFeedbackAt(sim_.now(), fb); }
  // Same, with an explicit "now" so tests and the ACK microbenchmark can script time.
  void onFeedbackAt(Time now, const FeedbackItem& fb) {
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
  // Records a packet as sent without emitting it (scripted tests and the ACK microbenchmark).
  void trackSent(std::uint64_t seq, Time send_ts, std::uint32_t size) {
    inflight_.insert(seq, Sent{send_ts, size, 0});
    inflight_bytes_ += size;
    next_seq_ = std::max(next_seq_, seq + 1);
    ++c_.sent_pkts;
    c_.sent_bytes += size;
  }
  void onControlTick(Time now) {
    cc_->onTimer(now);
    pacer_.setRate(cc_->pacingRateBps());
    while (!inflight_.empty() && now - inflight_.oldest().send_ts > lossTimeout())
      declareLost(inflight_.oldestSeq(), now, true);
    trySend();
    sim_.schedule(now + kControlInterval, [this] { onControlTick(sim_.now()); });
  }

  std::uint32_t flow() const { return flow_; }
  CC& cc() override { return *cc_; }
  const CC& cc() const override { return *cc_; }
  std::uint64_t inflightBytes() const override { return inflight_bytes_; }
  Duration srtt() const { return srtt_; }
  const SenderCounters& counters() const override { return c_; }

 private:
  void trySend() {
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
  void armSendTimer(Time at) {
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
  void sendOne(Time now) {
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
  void declareLost(std::uint64_t seq, Time now, bool timeout) {
    const LossInfo l{now, seq, inflight_.find(seq)->size, timeout};
    inflight_bytes_ -= l.bytes;
    inflight_.erase(seq);
    ++c_.lost_pkts;
    if (timeout) ++c_.timeouts;
    cc_->onLoss(l);
  }
  Duration lossTimeout() const { return std::max<Duration>(200'000'000, 2 * srtt_); }

  Simulator& sim_;
  PacketPool& pool_;
  std::uint32_t flow_;
  std::unique_ptr<CC> cc_;
  TrafficSource& source_;
  TxFn tx_;
  Pacer pacer_;
  bool started_ = false;
  std::uint64_t next_seq_ = 0;
  InflightRing inflight_;
  std::uint64_t inflight_bytes_ = 0;
  Duration srtt_ = 0;
  std::uint64_t timer_gen_ = 0;
  bool timer_armed_ = false;
  Time timer_at_ = 0;
  SenderCounters c_;
};

using Sender = BasicSender<CongestionControl>;

}  // namespace bcc
