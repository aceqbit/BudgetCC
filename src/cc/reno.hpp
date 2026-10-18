#pragma once
#include "cc/params.hpp"
#include "cc/window_cc.hpp"

namespace bcc {

class Reno final : public WindowCc {
 public:
  explicit Reno(ParamReader& p) : WindowCc(p.get("iw_pkts", 10.0)) {}

  void onAck(const AckInfo& a) override;
  void onLoss(const LossInfo& l) override;
  const char* name() const override { return "reno"; }
};

}  // namespace bcc
