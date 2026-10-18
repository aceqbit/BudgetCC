#include "harness/runner.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "cc/cc_interface.hpp"
#include "cc/dispatch.hpp"

namespace bcc {

namespace {
// splitmix64: deterministic flow start jitter derived from the run seed.
std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
}  // namespace

Experiment::Experiment(const RunConfig& cfg, const Trace& trace, std::uint32_t stream, SampleSink sink)
    : cfg_(cfg), trace_(trace), stream_(stream), sink_(std::move(sink)) {
  validate(cfg_);
  // E1: size every growing buffer up front so the steady-state loop never allocates.
  pool_.reserve(cfg_.buffer_pkts + kPoolSlack);
  q_series_.reserve(static_cast<std::size_t>(cfg_.duration / kSampleInterval) + 2);
  max_delivered_ = static_cast<std::size_t>(trace_.opportunities(0, cfg_.duration) * kOpportunityBytes / kWireBytes) + 16;
  link_ = std::make_unique<BottleneckLink>(sim_, pool_, trace_, cfg_.bufferBytes(), cfg_.prop_delay, [this](Packet* p) {
    flows_.at(p->flow)->rx->onPacket(p);
  });
  cc_cfg_.budget_ms = cfg_.budget_ms;
  cc_cfg_.params = cfg_.cc_params;
  for (std::size_t i = 0; i < cfg_.algos.size(); ++i)
    visitCongestionControl(cfg_.algos[i], cc_cfg_, [&](auto cc) { addFlow(i, std::move(cc)); });
  link_->start();
  sim_.schedule(kSampleInterval, [this] { sampleTick(); });
}

// Builds flow i around the concrete controller type CC so the sender's per-packet calls are not virtual (E5).
template <class CC>
void Experiment::addFlow(std::size_t i, std::unique_ptr<CC> cc) {
  auto f = std::make_unique<Flow>();
  const auto flow_id = static_cast<std::uint32_t>(i);
  if (cfg_.mode == "video") {
    auto v = std::make_unique<VideoSource>();
    f->video = v.get();
    f->source = std::move(v);
  } else {
    f->source = std::make_unique<BulkSource>();
  }
  Flow* fp = f.get();
  auto tx = std::make_unique<BasicSender<CC>>(sim_, pool_, flow_id, std::move(cc), *f->source,
                                              [this](Packet* p) { link_->onArrival(p); });
  BasicSender<CC>* typed = tx.get();
  f->rx = std::make_unique<Receiver>(sim_, pool_, flow_id, cfg_.prop_delay,
                                     [typed](const FeedbackItem& fb) { typed->onFeedback(fb); });
  f->rx->reserveSamples(max_delivered_);
  f->tx = std::move(tx);
  flows_.push_back(std::move(f));
  // Flow 0 starts at t = 0; later flows start within 10 ms, derived from the seed.
  const Time start = i == 0 ? 0 : static_cast<Time>(mix(cfg_.seed * 1000003ULL + i) % 10) * fromMs(1);
  fp->tx->start(start);
  if (fp->video != nullptr) sim_.schedule(start, [this, fp] { videoTick(*fp); });
}

Experiment::~Experiment() = default;

std::uint64_t Experiment::packetsSent() const {
  std::uint64_t n = 0;
  for (const auto& f : flows_) n += f->tx->counters().sent_pkts;
  return n;
}

void Experiment::videoTick(Flow& f) {
  f.video->generateFrame(sim_.now(), f.tx->cc().pacingRateBps());
  f.tx->kick();
  ++f.frame_index;
  sim_.schedule(VideoSource::frameTime(f.frame_index), [this, &f] { videoTick(f); });
}

void Experiment::sampleTick() {
  const Time now = sim_.now();
  const double q_ms = toMs(link_->queueDelay(now));
  q_series_.push_back(q_ms);
  const double cap_opps = static_cast<double>(trace_.opportunities(now - kSampleInterval, now));
  const double cap_mbps = cap_opps * kOpportunityBytes * 8.0 / toSec(kSampleInterval) / 1e6;
  for (std::size_t i = 0; i < flows_.size(); ++i) {
    Flow& f = *flows_[i];
    const ReceiverInterval iv = f.rx->takeInterval();
    if (iv.pkts > 0) f.last_owd_ms = iv.owd_ms_sum / static_cast<double>(iv.pkts);
    const std::uint64_t lost_total = f.tx->counters().lost_pkts;
    const CcProbe pr = f.tx->cc().probe();
    if (sink_) {
      Sample s;
      s.stream = stream_;
      s.run = cfg_.run_index;
      s.t_ms = static_cast<std::int32_t>(now / fromMs(1));
      s.flow = static_cast<std::int32_t>(i);
      s.inflight_kb = static_cast<std::int32_t>(std::llround(static_cast<double>(f.tx->inflightBytes()) / 1000.0));
      s.lost = static_cast<std::int32_t>(lost_total - f.last_lost);
      s.rate_mbps = static_cast<double>(iv.bytes) * 8.0 / toSec(kSampleInterval) / 1e6;
      s.cap_mbps = cap_mbps;
      s.q_ms = q_ms;
      s.owd_ms = f.last_owd_ms;
      s.chat_mbps = pr.chat_bps / 1e6;
      s.cwnd_kb = pr.cwnd_bytes / 1000.0;
      s.qest_ms = pr.qest_ms;
      sink_(s);
    }
    f.last_lost = lost_total;
  }
  if (++ticks_ % 10 == 0) checkInvariants();
  sim_.schedule(now + kSampleInterval, [this] { sampleTick(); });
}

void Experiment::checkInvariants() const {
  const LinkCounters& c = link_->counters();
  if (link_->queuedBytes() > cfg_.bufferBytes()) throw std::runtime_error("invariant: queue above buffer limit");
  if (c.delivered_bytes > trace_.capacityBytes(0, sim_.now() + 1))
    throw std::runtime_error("invariant: delivered bytes exceed the capacity integral");
}

void Experiment::checkConservation() const {
  const LinkCounters& c = link_->counters();
  std::uint64_t sent = 0, received = 0;
  for (const auto& f : flows_) {
    sent += f->tx->counters().sent_pkts;
    received += f->rx->counters().recv_pkts + f->rx->counters().bad_packets;
  }
  const std::uint64_t queued = link_->queuedPackets();
  auto fail = [](const std::string& m) { throw std::runtime_error("conservation violated: " + m); };
  if (sent != c.arrived_pkts) fail("sender packets != link arrivals");
  if (c.arrived_pkts != c.delivered_pkts + c.dropped_pkts + queued) fail("sent != delivered + dropped + in flight");
  if (received > c.delivered_pkts) fail("receivers saw more packets than the link released");
  if (pool_.counters().in_use != queued + (c.delivered_pkts - received)) fail("packet pool leak");
  if (c.delivered_bytes > trace_.capacityBytes(0, cfg_.duration)) fail("delivered bytes exceed capacity integral");
}

RunResult Experiment::finish() {
  if (sim_.now() < cfg_.duration) sim_.runUntil(cfg_.duration);
  checkConservation();
  SummaryInput in;
  for (const auto& f : flows_) {
    in.owd_ns.push_back(&f->rx->owdNs());
    in.flow_bytes.push_back(f->rx->counters().recv_bytes);
  }
  in.trace = &trace_;
  in.duration = cfg_.duration;
  in.budget_ms = cfg_.budget_ms;
  in.qstar_ms = cfg_.recoveryQstarMs();
  in.q_ms = &q_series_;
  RunResult r;
  r.cfg = cfg_;
  r.summary = computeSummary(in);
  r.dropped_pkts = link_->counters().dropped_pkts;
  r.events = sim_.eventsProcessed();
  r.sent_pkts = packetsSent();
  return r;
}

RunResult runOnce(const RunConfig& cfg, const Trace& trace, std::uint32_t stream, Experiment::SampleSink sink) {
  Experiment e(cfg, trace, stream, std::move(sink));
  e.runUntil(cfg.duration);
  return e.finish();
}

}  // namespace bcc
