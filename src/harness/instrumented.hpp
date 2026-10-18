#pragma once
#include "harness/runner.hpp"
#include "telemetry/csv_writer.hpp"
#include "telemetry/loki_pusher.hpp"

namespace bcc {

// Runs one experiment and streams its samples to the optional sinks. Pushes the run's summary line to Loki and
// waits for the pusher to drain, so consecutive runs never share the ring. Telemetry never changes the result.
RunResult runInstrumented(const RunConfig& cfg, const Trace& trace, TelemetrySink* loki, CsvWriter* csv);

}  // namespace bcc
