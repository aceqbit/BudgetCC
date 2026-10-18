#include <stdexcept>

#include "cc/bbr_lite.hpp"
#include "cc/budgetcc.hpp"
#include "cc/cc_interface.hpp"
#include "cc/cubic.hpp"
#include "cc/gcc_lite.hpp"
#include "cc/params.hpp"
#include "cc/reno.hpp"

namespace bcc {

const std::vector<std::string>& congestionControlNames() {
  static const std::vector<std::string> names = {"reno", "cubic", "bbr_lite", "gcc_lite", "budgetcc"};
  return names;
}

std::unique_ptr<CongestionControl> makeCongestionControl(const std::string& name, const CcConfig& cfg) {
  ParamReader p(cfg.params, name);
  std::unique_ptr<CongestionControl> cc;
  if (name == "reno") {
    cc = std::make_unique<Reno>(p);
  } else if (name == "cubic") {
    cc = std::make_unique<Cubic>(p);
  } else if (name == "bbr_lite") {
    cc = std::make_unique<BbrLite>(p);
  } else if (name == "gcc_lite") {
    cc = std::make_unique<GccLite>(p);
  } else if (name == "budgetcc") {
    cc = std::make_unique<BudgetCc>(p, cfg.budget_ms);
  } else {
    throw std::invalid_argument("unknown congestion control '" + name + "'");
  }
  p.finish();
  return cc;
}

}  // namespace bcc
