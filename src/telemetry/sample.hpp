#pragma once
// One 10 ms telemetry sample. Trivially copyable so it can travel through a lock-free ring.
#include <cstdint>
#include <type_traits>

namespace bcc {

struct Sample {
  std::uint32_t stream = 0;  // index into the label table registered with the telemetry sink
  std::uint32_t run = 0;     // numeric run id, formatted as "r0042"
  std::int32_t t_ms = 0;
  std::int32_t flow = 0;
  std::int32_t inflight_kb = 0;
  std::int32_t lost = 0;  // packets declared lost during the interval
  double rate_mbps = 0.0;  // goodput received during the interval
  double cap_mbps = 0.0;   // trace capacity during the interval
  double q_ms = 0.0;       // ground-truth queue delay
  double owd_ms = 0.0;     // mean one-way delay of packets received in the interval
  double chat_mbps = 0.0;  // controller capacity estimate (0 if none)
  double cwnd_kb = 0.0;    // controller window (0 if none)
  double qest_ms = 0.0;    // controller queue-delay estimate (0 if none)
};

static_assert(std::is_trivially_copyable_v<Sample>);
static_assert(sizeof(Sample) == 80, "Sample layout changed; the ring and the writers depend on it");

}  // namespace bcc
