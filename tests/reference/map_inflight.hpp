#pragma once
// Phase 1 in-flight table (std::map keyed by sequence), the oracle for the InflightRing differential test (E1).
#include <cstdint>
#include <map>

#include "transport/inflight_ring.hpp"

namespace bccref {

class MapInflight {
 public:
  bool empty() const { return m_.empty(); }
  std::size_t size() const { return m_.size(); }
  void insert(std::uint64_t seq, const bcc::Sent& s) { m_[seq] = s; }
  bcc::Sent* find(std::uint64_t seq) {
    auto it = m_.find(seq);
    return it == m_.end() ? nullptr : &it->second;
  }
  void erase(std::uint64_t seq) { m_.erase(seq); }
  std::uint64_t oldestSeq() const { return m_.begin()->first; }
  const bcc::Sent& oldest() const { return m_.begin()->second; }
  template <class F>
  void forEachBelow(std::uint64_t limit, F f) {
    std::vector<std::uint64_t> keys;
    for (auto& kv : m_) {
      if (kv.first >= limit) break;
      keys.push_back(kv.first);
    }
    for (std::uint64_t k : keys) {
      auto it = m_.find(k);
      if (it != m_.end()) f(k, it->second);
    }
  }

 private:
  std::map<std::uint64_t, bcc::Sent> m_;
};

}  // namespace bccref
