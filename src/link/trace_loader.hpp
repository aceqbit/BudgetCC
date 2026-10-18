#pragma once
// Mahimahi trace: each line is a millisecond timestamp granting one 1500-byte delivery opportunity.
#include <cstdint>
#include <string>
#include <vector>

#include "core/clock.hpp"
#include "core/packet.hpp"

namespace bcc {

class Trace {
 public:
  struct Opp {
    Time time;
    std::uint32_t count;  // opportunities sharing this millisecond
  };

  // Throws std::runtime_error("<name>:<line>: ...") on malformed input.
  static Trace parse(const std::string& text, const std::string& name);
  static Trace load(const std::string& path);

  const std::string& name() const { return name_; }
  Duration period() const { return period_; }
  std::uint64_t opportunitiesPerCycle() const { return cum_.back(); }

  // Opportunities with time in [t0, t1), trace looped.
  std::uint64_t opportunities(Time t0, Time t1) const;
  std::uint64_t capacityBytes(Time t0, Time t1) const { return opportunities(t0, t1) * kOpportunityBytes; }

  // Yields opportunities in time order forever (the trace loops).
  class Cursor {
   public:
    explicit Cursor(const Trace& t) : t_(&t) {}
    Opp next();

   private:
    const Trace* t_;
    std::size_t idx_ = 0;
    std::int64_t cycle_ = 0;
  };

 private:
  std::uint64_t countBefore(Time rel) const;

  std::string name_;
  std::vector<Time> times_;           // distinct, ascending, ns
  std::vector<std::uint32_t> counts_;  // multiplicity per distinct time
  std::vector<std::uint64_t> cum_;     // cum_[i] = sum of counts_[0..i)
  Duration period_ = 0;
};

}  // namespace bcc
