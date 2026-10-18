# BudgetCC design source (normative extract)

The full design document (problem statement, novelty table, tech-stack trade-offs, observability design, two-phase build order) was supplied by the project owner dated 18 Oct 2026. This file keeps the normative parts the code depends on. `CLAUDE.md` section 4 resolves its ambiguities.

## Problem
Single bottleneck, time-varying capacity C(t) from a Mahimahi delivery-opportunity trace, propagation 20 ms each way, drop-tail buffer (100 or 1000 packets of 1250 B). Sender uses only ACK timing. Maximise utilisation U = delivered bytes / integral C(t) dt subject to at most 5% of delivered packets having one-way delay above budget D.

Hypotheses (not results): (H1) at D = 150 ms violations <= 5% on most traces; (H2) utilisation within roughly 15-25 pp of the best throughput baseline on the same trace; (H3) p95 delay materially below Cubic's on the deep buffer.

## Matrix
10 traces (6 real, 4 synthetic) x {reno, cubic, bbr_lite, gcc_lite} x 2 buffers = 80; BudgetCC D in {100,150,300} x 10 x 2 = 60; video mode 5 algos x 10 traces x deep = 50; plus 60 fairness runs. 60 simulated seconds each.

## Per-packet flow
Simulator::step -> Sender::onSendTimer (pacer + in-flight cap) -> buildPacket (RTP-style header, seq, send ts) -> Link::onArrival (drop-tail) -> Link::onDeliveryOpportunity (1500 B credit per trace line) -> Receiver::onPacket (record, feedback after return delay) -> Sender::onAck (filters, loss detection) -> CongestionControl::onAck/onLoss, control step every 10 ms -> Pacer::setRate. Telemetry sample every 10 ms into a ring; separate pusher thread.

## Controller interface
```
struct AckInfo { Time now; Time send_ts; Time recv_ts; uint32_t bytes; uint64_t seq; Duration rtt; Duration owd; };
struct LossInfo { Time now; uint64_t seq; uint32_t bytes; bool timeout; };
class CongestionControl { virtual void onAck(const AckInfo&); virtual void onLoss(const LossInfo&);
  virtual void onTimer(Time now); virtual double pacingRateBps() const; virtual uint32_t inflightCapBytes() const;
  virtual const char* name() const; };
```

## BudgetCC (Phase 2)
```
on_ack(a):  owd_min.update(a.owd, 10 s); owd_s += (a.owd - owd_s)/8; delivered.add(a.now, a.bytes)
on_timer(now):                                   # every 10 ms
  q = max(0, owd_s - owd_min)
  sample = delivered.rate(clamp(srtt, 20 ms, 200 ms))
  if sample < C_hat: C_hat = 0.5*C_hat + 0.5*sample else: C_hat += 0.1*(sample - C_hat)
  if delivered.rate(20 ms) < 0.5*C_hat: C_hat = delivered.rate(20 ms)
  Qstar = max(10 ms, (D - owd_min)*(1 - margin)); e = (Qstar - q)/Qstar
  rate = C_hat * clamp(1 + k*e, 0.5, 1.15)
  if q > Qstar: rate = min(rate, C_hat*Qstar/q)
  rate = max(rate, floor_rate); inflight_cap = rate*(rtt_min + Qstar)
```
Start values: D 150 ms, margin 0.2, k 0.5, up/down weights 0.1/0.5, probe ceiling 1.15, floor 100 kbps. Burst of >= 3 losses within one RTT cuts C_hat by 20%; isolated losses ignored.

BudgetCC v0 (Phase 1): threshold AIMD on queue delay; below Q* add one packet per RTT, above Q* multiply rate by 0.8.

## Phase 1 vs Phase 2
Phase 1: priority_queue of std::function events, new/delete per packet, std::map in-flight, linear-scan deques, one ACK per packet, mutex ring + std::string JSON, push once per second. Phase 2: E1 POD events + packet pool + ring in-flight; E2 binary heap; E3 O(1) filters; E4 SPSC ring + format_to; E5 no virtual dispatch on hot path; E6 LTO; E7 cache-line separation; A1 BudgetCC controller above; A2 sweep k {0.25,0.5,1.0} x margin {0.1,0.2,0.3} x down weight {0.3,0.5,0.7}; A3 receiver batches feedback every 10 ms; A4 adaptive margin.

## Improvement formulas
Lower-is-better: (P1 - P2)/P1 x 100. Higher-is-better: (P2 - P1)/P1 x 100. Utilisation additionally in percentage points. Versus a baseline: same formula with the baseline's Phase 2 value.

## Must not claim
First delay-budget controller; beating SCReAM or Copa (not implemented); baselines equal to the kernel's.

## Sources
Sprout (NSDI 2013), Verus (SIGCOMM 2015), Copa (NSDI 2018), BBR (ACM Queue 2016), CUBIC (2008) / RFC 8312, Mahimahi (ATC 2015), GCC (MMSys 2016), RFC 8298, RFC 8698, RFC 3550, ITU-T G.114.
