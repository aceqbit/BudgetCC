#include <stdexcept>

#include "cc/cc_interface.hpp"
#include "cc/cubic.hpp"
#include "cc/params.hpp"
#include "cc/reno.hpp"

namespace bcc {

const std::vector<std::string>& congestionControlNames() {
  static const std::vector<std::string> names = {"reno", "cubic"};
  return names;
}

std::unique_ptr<CongestionControl> makeCongestionControl(const std::string& name, const CcConfig& cfg) {
  ParamReader p(cfg.params, name);
  std::unique_ptr<CongestionControl> cc;
  if (name == "reno") {
    cc = std::make_unique<Reno>(p);
  } else if (name == "cubic") {
    cc = std::make_unique<Cubic>(p);
  } else {
    throw std::invalid_argument("unknown congestion control '" + name + "'");
  }
  p.finish();
  return cc;
}

}  // namespace bcc
