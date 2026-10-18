#include "harness/instrumented.hpp"

#include "harness/results.hpp"

namespace bcc {

RunResult runInstrumented(const RunConfig& cfg, const Trace& trace, TelemetrySink* loki, CsvWriter* csv) {
  std::uint32_t stream = 0;
  if (loki != nullptr) stream = loki->addStream(lokiLabels(cfg, "sample"), lokiLabels(cfg, "summary"));
  Experiment::SampleSink sink;
  if (loki != nullptr || csv != nullptr) {
    sink = [loki, csv](const Sample& s) {
      if (loki != nullptr) loki->sample(s);
      if (csv != nullptr) csv->write(s);
    };
  }
  RunResult r = runOnce(cfg, trace, stream, sink);
  if (loki != nullptr) {
    loki->summary(stream, runResultToJson(r), cfg.duration / fromMs(1) + 1);
    loki->flush();
  }
  return r;
}

}  // namespace bcc
