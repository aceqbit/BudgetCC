#pragma once
// Reads --cc-param overrides and remembers which keys a controller consumed.
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace bcc {

class ParamReader {
 public:
  ParamReader(const std::map<std::string, double>& m, std::string algo) : m_(m), algo_(std::move(algo)) {}

  double get(const std::string& key, double def) {
    used_.insert(key);
    auto it = m_.find(key);
    return it == m_.end() ? def : it->second;
  }
  // Throws if the user supplied a key the controller never asked for.
  void finish() const {
    for (const auto& kv : m_)
      if (used_.count(kv.first) == 0)
        throw std::invalid_argument("unknown cc-param '" + kv.first + "' for algorithm '" + algo_ + "'");
  }

 private:
  const std::map<std::string, double>& m_;
  std::string algo_;
  std::set<std::string> used_;
};

}  // namespace bcc
