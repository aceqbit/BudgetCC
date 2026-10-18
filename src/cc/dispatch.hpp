#pragma once
// Builds the named controller as its concrete type and hands it to f (E5). Generic code instantiated
// per controller then calls it without virtual dispatch. Throws std::invalid_argument like the factory.
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "cc/bbr_lite.hpp"
#include "cc/budgetcc.hpp"
#include "cc/cc_interface.hpp"
#include "cc/cubic.hpp"
#include "cc/gcc_lite.hpp"
#include "cc/params.hpp"
#include "cc/reno.hpp"

namespace bcc {

template <class CC, class F, class... Extra>
decltype(auto) visitBuilt(const std::string& name, const CcConfig& cfg, F&& f, Extra... extra) {
  ParamReader p(cfg.params, name);
  auto cc = std::make_unique<CC>(p, extra...);
  p.finish();
  return std::forward<F>(f)(std::move(cc));
}

template <class F>
decltype(auto) visitCongestionControl(const std::string& name, const CcConfig& cfg, F&& f) {
  if (name == "reno") return visitBuilt<Reno>(name, cfg, std::forward<F>(f));
  if (name == "cubic") return visitBuilt<Cubic>(name, cfg, std::forward<F>(f));
  if (name == "bbr_lite") return visitBuilt<BbrLite>(name, cfg, std::forward<F>(f));
  if (name == "gcc_lite") return visitBuilt<GccLite>(name, cfg, std::forward<F>(f));
  if (name == "budgetcc") return visitBuilt<BudgetCc>(name, cfg, std::forward<F>(f), cfg.budget_ms);
  throw std::invalid_argument("unknown congestion control '" + name + "'");
}

}  // namespace bcc
