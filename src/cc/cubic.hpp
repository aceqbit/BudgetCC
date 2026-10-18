#pragma once
#include <cmath>

#include "cc/params.hpp"
#include "cc/window_cc.hpp"

namespace bcc {

// CUBIC per RFC 8312 (C = 0.4, beta = 0.7, fast convergence, TCP-friendly region). No HyStart.
class Cubic final : public WindowCc {
 public:
  static constexpr double kC = 0.4;
  static constexpr double kBeta = 0.7;

  explicit Cubic(ParamReader& p) : WindowCc(p.get("iw_pkts", 10.0)) {}

  // RFC 8312 closed forms, window in packets and time in seconds since the epoch start.
  static double cubicK(double w_max) { return std::cbrt(w_max * (1.0 - kBeta) / kC); }
  static double cubicWindow(double t_s, double w_max) {
    const double d = t_s - cubicK(w_max);
    return kC * d * d * d + w_max;
  }

  void onAck(const AckInfo& a) override;
  void onLoss(const LossInfo& l) override;
  const char* name() const override { return "cubic"; }

  double wMaxPkts() const { return w_max_; }

 private:
  Duration min_rtt_ = 0;
  double w_max_ = 0.0;
  double w_last_max_ = 0.0;
  double w_est_ = 0.0;
  double k_ = 0.0;
  double origin_ = 0.0;
  bool epoch_ = false;
  Time epoch_start_ = 0;
};

}  // namespace bcc
