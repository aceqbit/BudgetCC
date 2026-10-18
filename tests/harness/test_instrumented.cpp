#include <gtest/gtest.h>

#include <httplib.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

#include "harness/cli.hpp"
#include "harness/instrumented.hpp"
#include "harness/matrix.hpp"
#include "harness/results.hpp"
#include "support/trace_util.hpp"

using namespace bcc;

namespace {
const Trace& const12() {
  static const Trace t = Trace::parse(bcc_test::constantTrace(12.0, 60000), "const12");
  return t;
}
RunConfig cfg(const std::string& algo, double secs) {
  RunConfig c;
  c.algos = {algo};
  c.trace = "const12";
  c.buffer_pkts = 1000;
  c.duration = static_cast<Duration>(secs * 1e9);
  return c;
}
struct Mock {
  httplib::Server svr;
  std::thread th;
  int port = 0;
  std::mutex m;
  std::vector<std::string> bodies;
  Mock() {
    svr.Post("/loki/api/v1/push", [this](const httplib::Request& req, httplib::Response& res) {
      std::lock_guard<std::mutex> lk(m);
      bodies.push_back(req.body);
      res.status = 204;
    });
    port = svr.bind_to_any_port("127.0.0.1");
    th = std::thread([this] { svr.listen_after_bind(); });
    svr.wait_until_ready();
  }
  std::vector<std::string> snapshot() {
    std::lock_guard<std::mutex> lk(m);
    return bodies;
  }
  ~Mock() {
    svr.stop();
    th.join();
  }
};
std::size_t count(const std::string& hay, const std::string& needle) {
  std::size_t n = 0, pos = 0;
  while ((pos = hay.find(needle, pos)) != std::string::npos) {
    ++n;
    pos += needle.size();
  }
  return n;
}
}  // namespace

TEST(Instrumented, TelemetryOnAndOffGiveIdenticalSummaries) {
  const RunConfig c = cfg("cubic", 10.0);
  LokiConfig lc;  // null sink: format and count only
  lc.flush_interval_ms = 10;
  TelemetrySink sink(lc);
  const std::string with = runResultToJson(runInstrumented(c, const12(), &sink, nullptr));
  const std::string without = runResultToJson(runOnce(c, const12()));
  EXPECT_EQ(with, without);
  sink.flush();
  EXPECT_EQ(sink.counters().pushed_lines, 999u + 1u);  // one sample per 10 ms tick below 10 s, plus the summary
}

TEST(Instrumented, SamplesAndSummaryReachLokiWithTheRunLabels) {
  Mock mock;
  LokiConfig lc;
  lc.url = "http://127.0.0.1:" + std::to_string(mock.port);
  lc.flush_interval_ms = 10;
  lc.base_ns = 1'700'000'000'000'000'000LL;
  RunConfig c = cfg("reno", 5.0);
  c.run_index = 12;
  c.phase = "p2";
  {
    TelemetrySink sink(lc);
    const RunResult r = runInstrumented(c, const12(), &sink, nullptr);
    EXPECT_GT(r.summary.delivered_bytes, 0u);
    EXPECT_EQ(sink.counters().push_failures, 0u);
  }
  std::string all;
  const auto bodies = mock.snapshot();
  for (const auto& b : bodies) all += b;
  EXPECT_EQ(count(all, "\"kind\":\"sample\""), bodies.size() - 1);  // one body per sample batch + one summary
  EXPECT_EQ(count(all, "\"kind\":\"summary\""), 1u);
  EXPECT_NE(all.find("\"phase\":\"p2\""), std::string::npos);
  EXPECT_NE(all.find("\"algo\":\"reno\""), std::string::npos);
  EXPECT_NE(all.find(R"(\"run\":\"r0012\")"), std::string::npos);
  EXPECT_NE(all.find("utilisation_pct"), std::string::npos);
  EXPECT_EQ(count(all, "{\\\"t_ms\\\":"), 499u);  // one line per 10 ms tick
}

TEST(Instrumented, CsvAndLokiTogether) {
  const auto path = (std::filesystem::temp_directory_path() / "bcc_instr.csv").string();
  CsvWriter csv(path);
  LokiConfig lc;
  lc.flush_interval_ms = 10;
  TelemetrySink sink(lc);
  runInstrumented(cfg("cubic", 2.0), const12(), &sink, &csv);
  csv.close();
  int rows = -1;  // header
  {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) ++rows;
  }
  EXPECT_EQ(rows, 199);
  std::filesystem::remove(path);
}

TEST(Instrumented, UnreachableLokiFailsTheCliButNotTheSimulation) {
  std::ostringstream out, err;
  const int rc = runMain({"budgetcc", "run", "--algo", "reno", "--trace", "step_down", "--buffer", "100", "--duration-s",
                          "1", "--loki", "http://999.999.999.999:1"},
                         out, err);
  EXPECT_EQ(rc, 1);
  EXPECT_NE(err.str().find("Loki push failed"), std::string::npos);
  EXPECT_NE(out.str().find("utilisation_pct"), std::string::npos) << "the run itself still completed";
}

TEST(Instrumented, CliWritesCsvAndOutFile) {
  const auto dir = std::filesystem::temp_directory_path() / "bcc_cli_csv";
  std::filesystem::remove_all(dir);
  const auto out_file = (std::filesystem::temp_directory_path() / "bcc_cli_out.json").string();
  std::ostringstream out, err;
  ASSERT_EQ(runMain({"budgetcc", "run", "--algo", "reno", "--trace", "step_down", "--buffer", "100", "--duration-s", "1",
                     "--csv", dir.string(), "--out", out_file},
                    out, err),
            0)
      << err.str();
  int files = 0;
  for (const auto& e : std::filesystem::directory_iterator(dir)) {
    ++files;
    EXPECT_NE(e.path().filename().string().find("reno_step_down_100"), std::string::npos);
  }
  EXPECT_EQ(files, 1);
  std::string line;
  {
    std::ifstream f(out_file);
    std::getline(f, line);
  }
  EXPECT_EQ(line + "\n", out.str());
  std::filesystem::remove_all(dir);
  std::filesystem::remove(out_file);
}

TEST(Instrumented, MatrixSubcommandWritesAResultsFile) {
  const auto out_file = (std::filesystem::temp_directory_path() / "bcc_matrix.json").string();
  std::ostringstream out, err;
  int rc = runMain({"budgetcc", "matrix", "--phase", "p1", "--out", out_file, "--duration-s", "1", "--limit", "3"}, out,
                   err);
  if (rc == 1 && err.str().find("not found") != std::string::npos) GTEST_SKIP() << "real traces not fetched";
  ASSERT_EQ(rc, 0) << err.str();
  std::stringstream ss;
  {
    std::ifstream f(out_file);
    ss << f.rdbuf();
  }
  EXPECT_NE(ss.str().find("\"phase\":\"p1\""), std::string::npos);
  EXPECT_EQ(count(ss.str(), "\"run\":\"r000"), 3u);
  std::filesystem::remove(out_file);
  std::ostringstream o2, e2;
  EXPECT_EQ(runMain({"budgetcc", "matrix", "--phase", "p9", "--out", out_file}, o2, e2), 2);
}

TEST(Instrumented, MatrixFiltersKeepFullMatrixRunIds) {
  MatrixOptions mo;
  mo.phase = "p1";
  mo.duration = fromSec(1);
  mo.traces = {"step_down"};
  mo.mode = "bulk";
  mo.buffer = 1000;
  std::vector<std::string> ids;
  mo.on_done = [&](const RunConfig& c, const RunResult&) { ids.push_back(c.runId() + c.algoLabel()); };
  runMatrix(mo);
  // 4 baselines + 3 budgets of BudgetCC, each on step_down with the deep buffer
  ASSERT_EQ(ids.size(), 7u);
  const auto full = expandMatrix("p1");
  for (const auto& c : full)
    if (c.trace == "step_down" && c.mode == "bulk" && c.buffer_pkts == 1000) {
      bool found = false;
      for (const auto& id : ids) found |= id == c.runId() + c.algoLabel();
      EXPECT_TRUE(found) << c.runId();
    }
}
