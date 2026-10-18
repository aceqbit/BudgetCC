#include "harness/results.hpp"

#include <array>
#include <cstdio>
#include <memory>

#include "core/json.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#define BCC_HAVE_CPUID 1
#endif

#ifndef BUDGETCC_GIT_SHA
#define BUDGETCC_GIT_SHA "unknown"
#endif
#ifndef BUDGETCC_BUILD_TYPE
#define BUDGETCC_BUILD_TYPE "unknown"
#endif
#ifndef BUDGETCC_FLAGS
#define BUDGETCC_FLAGS ""
#endif

namespace bcc {

namespace {
std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && s[i] == ' ') ++i;
  return s.substr(i);
}

std::string capture(const char* cmd) {
#ifdef _WIN32
  std::unique_ptr<FILE, int (*)(FILE*)> pipe(_popen(cmd, "r"), _pclose);
#else
  std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
#endif
  if (!pipe) return "unknown";
  std::array<char, 256> buf{};
  std::string out;
  while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) out += buf.data();
  out = trim(out);
  return out.empty() ? "unknown" : out;
}

std::string cpuName() {
#ifdef BCC_HAVE_CPUID
  std::array<unsigned, 12> r{};
#if defined(_MSC_VER)
  std::array<int, 4> regs{};
  for (unsigned i = 0; i < 3; ++i) {
    __cpuid(regs.data(), static_cast<int>(0x80000002U + i));
    for (unsigned j = 0; j < 4; ++j) r[i * 4 + j] = static_cast<unsigned>(regs[j]);
  }
#else
  for (unsigned i = 0; i < 3; ++i) __get_cpuid(0x80000002U + i, &r[i * 4], &r[i * 4 + 1], &r[i * 4 + 2], &r[i * 4 + 3]);
#endif
  char name[49] = {};
  for (std::size_t i = 0; i < 12; ++i)
    for (std::size_t b = 0; b < 4; ++b) name[i * 4 + b] = static_cast<char>((r[i] >> (8 * b)) & 0xffU);
  const std::string s = trim(name);
  if (!s.empty()) return s;
#endif
  return "unknown";
}

std::string osName() {
#ifdef _WIN32
  return capture("cmd /c ver");
#else
  return capture("uname -sr");
#endif
}

std::string powerPlan() {
#ifdef _WIN32
  return capture("powercfg /getactivescheme");
#else
  return "n/a";
#endif
}

void compilerInfo(std::string& name, std::string& version) {
#if defined(__clang__)
  name = "clang";
  version = __clang_version__;
#elif defined(__GNUC__)
  name = "gcc";
  version = __VERSION__;
#elif defined(_MSC_VER)
  name = "msvc";
  version = std::to_string(_MSC_FULL_VER);
#else
  name = "unknown";
  version = "unknown";
#endif
}
}  // namespace

std::string environmentJson() {
  std::string name, version;
  compilerInfo(name, version);
  JsonObject o;
  o.str("cpu", cpuName())
      .str("os", osName())
      .str("compiler", name)
      .str("version", version)
      .str("flags", BUDGETCC_FLAGS)
      .str("build_type", BUDGETCC_BUILD_TYPE)
      .str("power_plan", powerPlan());
  return o.finish();
}

std::string runResultToJson(const RunResult& r) {
  const Summary& s = r.summary;
  JsonObject o;
  o.str("run", r.cfg.runId())
      .str("algo", r.cfg.algoLabel())
      .str("trace", r.cfg.trace)
      .i64("buffer", r.cfg.buffer_pkts)
      .num("budget_ms", r.cfg.budget_ms)
      .str("mode", r.cfg.mode)
      .u64("flows", r.cfg.algos.size())
      .num("utilisation_pct", s.utilisation_pct)
      .num("p95_owd_ms", s.p95_owd_ms)
      .num("p99_owd_ms", s.p99_owd_ms)
      .num("violation_pct", s.violation_pct)
      .num("violation_pct_d100", s.violation_pct_d100)
      .num("violation_pct_d150", s.violation_pct_d150)
      .num("violation_pct_d300", s.violation_pct_d300);
  if (s.recovery.median_ms) {
    o.num("recovery_ms", *s.recovery.median_ms);
  } else {
    o.null("recovery_ms");
  }
  o.u64("recovery_censored", s.recovery.censored);
  if (s.jain) {
    o.num("jain", *s.jain);
  } else {
    o.null("jain");
  }
  o.u64("delivered_bytes", s.delivered_bytes).u64("dropped_pkts", r.dropped_pkts).u64("events", r.events);
  return o.finish();
}

std::string resultsFileJson(const ResultsHeader& h, const std::vector<std::string>& run_entries) {
  std::string runs = "[";
  for (std::size_t i = 0; i < run_entries.size(); ++i) {
    if (i != 0) runs += ",\n";
    runs += run_entries[i];
  }
  runs += "]";
  JsonObject o;
  o.i64("schema", 1)
      .str("phase", h.phase)
      .str("git_sha", BUDGETCC_GIT_SHA)
      .raw("env", environmentJson())
      .num("matrix_runtime_s", h.matrix_runtime_s)
      .raw("runs", runs);
  return o.finish();
}

}  // namespace bcc
