#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cc/cc_interface.hpp"
#include "cc/dispatch.hpp"

namespace bcc {

const std::vector<std::string>& congestionControlNames() {
  static const std::vector<std::string> names = {"reno", "cubic", "bbr_lite", "gcc_lite", "budgetcc"};
  return names;
}

std::unique_ptr<CongestionControl> makeCongestionControl(const std::string& name, const CcConfig& cfg) {
  return visitCongestionControl(name, cfg, [](auto cc) { return std::unique_ptr<CongestionControl>(std::move(cc)); });
}

}  // namespace bcc
