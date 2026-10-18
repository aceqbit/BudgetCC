#pragma once
#include <memory>
#include <vector>

#include "cc/cc_interface.hpp"

namespace bcc_test {

// Fixed-rate, fixed-cap controller that records everything the sender tells it.
class FakeCc final : public bcc::CongestionControl {
 public:
  FakeCc(double rate_bps, std::uint32_t cap_bytes) : rate_(rate_bps), cap_(cap_bytes) {}
  void onAck(const bcc::AckInfo& a) override { acks.push_back(a); }
  void onLoss(const bcc::LossInfo& l) override { losses.push_back(l); }
  void onTimer(bcc::Time now) override { timers.push_back(now); }
  double pacingRateBps() const override { return rate_; }
  std::uint32_t inflightCapBytes() const override { return cap_; }
  const char* name() const override { return "fake"; }

  void setRate(double r) { rate_ = r; }
  void setCap(std::uint32_t c) { cap_ = c; }

  std::vector<bcc::AckInfo> acks;
  std::vector<bcc::LossInfo> losses;
  std::vector<bcc::Time> timers;

 private:
  double rate_;
  std::uint32_t cap_;
};

}  // namespace bcc_test
