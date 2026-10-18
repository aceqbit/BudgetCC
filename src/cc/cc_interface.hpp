#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/clock.hpp"

namespace bcc {

struct AckInfo {
  Time now;
  Time send_ts;
  Time recv_ts;
  std::uint32_t bytes;
  std::uint64_t seq;
  Duration rtt;
  Duration owd;
};

struct LossInfo {
  Time now;
  std::uint64_t seq;
  std::uint32_t bytes;
  bool timeout;
};

// Controller internals exposed for telemetry samples and tests. Zero means "not applicable".
struct CcProbe {
  double cwnd_bytes = 0.0;
  double chat_bps = 0.0;  // capacity / bottleneck-bandwidth estimate
  double qest_ms = 0.0;   // estimated queue delay
  double qstar_ms = 0.0;  // queue-delay target
  double min_rtt_ms = 0.0;
};

class CongestionControl {
 public:
  virtual ~CongestionControl() = default;
  virtual void onAck(const AckInfo&) = 0;
  virtual void onLoss(const LossInfo&) = 0;
  virtual void onTimer(Time now) = 0;  // control step, every 10 ms
  virtual double pacingRateBps() const = 0;
  virtual std::uint32_t inflightCapBytes() const = 0;
  virtual const char* name() const = 0;
  virtual CcProbe probe() const { return {}; }
};

struct CcConfig {
  double budget_ms = 150.0;                // application one-way delay budget D (BudgetCC)
  std::map<std::string, double> params;    // --cc-param overrides
};

// Throws std::invalid_argument for an unknown algorithm or an unknown parameter key.
std::unique_ptr<CongestionControl> makeCongestionControl(const std::string& name, const CcConfig& cfg);
const std::vector<std::string>& congestionControlNames();

}  // namespace bcc
