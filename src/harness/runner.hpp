#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/packet_pool.hpp"
#include "core/simulator.hpp"
#include "harness/config.hpp"
#include "harness/summary.hpp"
#include "link/bottleneck_link.hpp"
#include "telemetry/sample.hpp"
#include "transport/receiver.hpp"
#include "transport/sender.hpp"
#include "transport/traffic_source.hpp"

namespace bcc {

struct RunResult {
  RunConfig cfg;
  Summary summary;
  std::uint64_t dropped_pkts = 0;
  std::uint64_t events = 0;
  std::uint64_t sent_pkts = 0;
};

// One simulated run: N flows sharing one trace-driven bottleneck.
class Experiment {
 public:
  using SampleSink = std::function<void(const Sample&)>;
  static constexpr Duration kSampleInterval = 10'000'000;

  // `trace` must outlive the experiment. `stream` labels samples for the telemetry sink.
  Experiment(const RunConfig& cfg, const Trace& trace, std::uint32_t stream = 0, SampleSink sink = {});
  ~Experiment();
  Experiment(const Experiment&) = delete;
  Experiment& operator=(const Experiment&) = delete;

  void runUntil(Time t) { sim_.runUntil(t); }
  // Runs to cfg.duration if needed, verifies conservation (throws std::runtime_error) and computes the summary.
  RunResult finish();

  Simulator& sim() { return sim_; }
  PacketPool& pool() { return pool_; }
  BottleneckLink& link() { return *link_; }
  std::size_t flows() const { return flows_.size(); }
  Sender& sender(std::size_t i) { return *flows_[i]->tx; }
  Receiver& receiver(std::size_t i) { return *flows_[i]->rx; }
  const std::vector<double>& queueSeries() const { return q_series_; }
  std::uint64_t packetsSent() const;

 private:
  struct Flow {
    std::unique_ptr<TrafficSource> source;
    VideoSource* video = nullptr;
    std::unique_ptr<Receiver> rx;
    std::unique_ptr<Sender> tx;
    std::uint64_t last_lost = 0;
    double last_owd_ms = 0.0;
    std::uint64_t frame_index = 0;
  };

  void sampleTick();
  void videoTick(Flow& f);
  void checkInvariants() const;
  void checkConservation() const;

  RunConfig cfg_;
  const Trace& trace_;
  std::uint32_t stream_;
  SampleSink sink_;
  Simulator sim_;
  PacketPool pool_;
  std::unique_ptr<BottleneckLink> link_;
  std::vector<std::unique_ptr<Flow>> flows_;
  std::vector<double> q_series_;
  std::uint64_t ticks_ = 0;
};

// Convenience: build, run for cfg.duration, finish.
RunResult runOnce(const RunConfig& cfg, const Trace& trace, std::uint32_t stream = 0,
                  Experiment::SampleSink sink = {});

}  // namespace bcc
