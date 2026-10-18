#include <gtest/gtest.h>

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "telemetry/loki_pusher.hpp"

using namespace bcc;

namespace {
struct MockLoki {
  httplib::Server svr;
  std::thread th;
  int port = 0;
  std::mutex m;
  std::vector<std::string> bodies;
  std::atomic<int> fail_first{0};
  std::atomic<int> fail_status{429};
  std::atomic<int> requests{0};

  MockLoki() {
    svr.Post("/loki/api/v1/push", [this](const httplib::Request& req, httplib::Response& res) {
      std::lock_guard<std::mutex> lk(m);
      const int n = ++requests;
      if (n <= fail_first) {
        res.status = fail_status;
        return;
      }
      EXPECT_EQ(req.get_header_value("Content-Type"), "application/json");
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
  ~MockLoki() {
    svr.stop();
    th.join();
  }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
};

Labels sampleLabels() {
  return {{"app", "budgetcc"}, {"kind", "sample"}, {"algo", "cubic"},  {"trace", "step_down"},
          {"phase", "p1"},     {"buffer", "1000"}, {"budget", "none"}, {"mode", "bulk"}};
}
Labels summaryLabels() {
  Labels l = sampleLabels();
  l["kind"] = "summary";
  return l;
}

// All (ns, line-count) values of a push body, in order.
std::vector<std::int64_t> timestamps(const std::string& body) {
  std::vector<std::int64_t> ts;
  std::size_t pos = 0;
  while ((pos = body.find("[\"", pos)) != std::string::npos) {
    pos += 2;
    std::size_t end = pos;
    while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end]))) ++end;
    if (end > pos && body.compare(end, 2, "\",") == 0) ts.push_back(std::stoll(body.substr(pos, end - pos)));
  }
  return ts;
}

LokiConfig fast(const std::string& url) {
  LokiConfig c;
  c.url = url;
  c.batch_lines = 1000;
  c.backoff_ms = 1;
  c.flush_interval_ms = 20;
  c.base_ns = 1'700'000'000'000'000'000LL;
  return c;
}

Sample at(int t_ms, std::uint32_t stream = 0, int flow = 0) {
  Sample s;
  s.stream = stream;
  s.t_ms = t_ms;
  s.flow = flow;
  s.run = 7;
  return s;
}
}  // namespace

TEST(LokiPusher, BodyHasTheExactPushApiShape) {
  StreamLines s;
  s.labels = {{"app", "budgetcc"}, {"kind", "sample"}};
  s.add(1700000000000000001LL, R"({"a":1})");
  s.add(1700000000000000002LL, "x\"y");
  EXPECT_EQ(s.size(), 2u);
  EXPECT_EQ(s.line(0), R"({"a":1})");
  EXPECT_EQ(s.line(1), "x\"y");
  auto body = [](const std::vector<StreamLines>& v) {
    std::string out;
    appendPushBody(out, v);
    return out;
  };
  EXPECT_EQ(body({s}),
            R"({"streams":[{"stream":{"app":"budgetcc","kind":"sample"},"values":[["1700000000000000001","{\"a\":1}"],["1700000000000000002","x\"y"]]}]})");
  EXPECT_EQ(body({}), R"({"streams":[]})");
  StreamLines empty;
  EXPECT_EQ(body({empty}), R"({"streams":[]})");
  s.clear();
  EXPECT_EQ(s.size(), 0u);
  s.addWith(5, [](std::string& text) { text += "w"; });
  EXPECT_EQ(s.line(0), "w");
  EXPECT_EQ(body({s}), R"({"streams":[{"stream":{"app":"budgetcc","kind":"sample"},"values":[["5","w"]]}]})");
}

TEST(LokiPusher, PushesBatchesWithExactLabelsAndStrictlyIncreasingTimestamps) {
  MockLoki mock;
  {
    TelemetrySink sink(fast(mock.url()));
    const auto id = sink.addStream(sampleLabels(), summaryLabels());
    for (int t = 1; t <= 5000; ++t) sink.sample(at(t, id));
    sink.summary(id, R"({"run":"r0007","utilisation_pct":91.5})", 5001);
    sink.flush();
    const auto c = sink.counters();
    EXPECT_EQ(c.pushed_lines, 5001u);
    EXPECT_EQ(c.push_failures, 0u);
    EXPECT_GT(c.bytes_written, 5000u * 60u);
  }
  std::vector<std::int64_t> sample_ts;
  std::size_t summary_bodies = 0;
  for (const auto& b : mock.snapshot()) {
    const auto ts = timestamps(b);
    EXPECT_GE(ts.size(), 1u);
    EXPECT_LE(ts.size(), 1000u);  // batch limit
    if (b.find("\"kind\":\"summary\"") != std::string::npos) {
      ++summary_bodies;
      EXPECT_NE(b.find(R"("stream":{"algo":"cubic","app":"budgetcc","budget":"none","buffer":"1000","kind":"summary","mode":"bulk","phase":"p1","trace":"step_down"})"),
                std::string::npos);
      EXPECT_NE(b.find("utilisation_pct"), std::string::npos);
    } else {
      EXPECT_NE(b.find(R"("stream":{"algo":"cubic","app":"budgetcc","budget":"none","buffer":"1000","kind":"sample","mode":"bulk","phase":"p1","trace":"step_down"})"),
                std::string::npos);
      sample_ts.insert(sample_ts.end(), ts.begin(), ts.end());
    }
  }
  EXPECT_EQ(summary_bodies, 1u);
  ASSERT_EQ(sample_ts.size(), 5000u);
  for (std::size_t i = 1; i < sample_ts.size(); ++i) ASSERT_GT(sample_ts[i], sample_ts[i - 1]);
  EXPECT_EQ(sample_ts.front(), 1'700'000'000'000'000'000LL + 1'000'000);  // base + simulated time
}

TEST(LokiPusher, TwoFlowsAtTheSameTickKeepTimestampsStrictlyIncreasing) {
  MockLoki mock;
  {
    TelemetrySink sink(fast(mock.url()));
    const auto id = sink.addStream(sampleLabels(), summaryLabels());
    for (int t = 10; t <= 100; t += 10) {
      sink.sample(at(t, id, 0));
      sink.sample(at(t, id, 1));
    }
    sink.flush();
  }
  std::vector<std::int64_t> ts;
  for (const auto& b : mock.snapshot()) {
    const auto v = timestamps(b);
    ts.insert(ts.end(), v.begin(), v.end());
  }
  ASSERT_EQ(ts.size(), 20u);
  for (std::size_t i = 1; i < ts.size(); ++i) ASSERT_GT(ts[i], ts[i - 1]);
}

TEST(LokiPusher, RetriesWithBackoffOn429And5xx) {
  for (int status : {429, 503}) {
    MockLoki mock;
    mock.fail_first = 2;
    mock.fail_status = status;
    TelemetrySink sink(fast(mock.url()));
    const auto id = sink.addStream(sampleLabels(), summaryLabels());
    for (int t = 1; t <= 10; ++t) sink.sample(at(t, id));
    sink.flush();
    const auto c = sink.counters();
    EXPECT_EQ(c.pushed_lines, 10u) << status;
    EXPECT_EQ(c.push_failures, 0u) << status;
    EXPECT_EQ(mock.requests, 3) << status;  // two rejected attempts, then success
    EXPECT_EQ(mock.snapshot().size(), 1u);
  }
}

TEST(LokiPusher, GivesUpAfterMaxAttemptsAndCountsTheFailure) {
  MockLoki mock;
  mock.fail_first = 1000;
  mock.fail_status = 500;
  LokiConfig cfg = fast(mock.url());
  cfg.max_attempts = 3;
  TelemetrySink sink(cfg);
  const auto id = sink.addStream(sampleLabels(), summaryLabels());
  for (int t = 1; t <= 10; ++t) sink.sample(at(t, id));
  sink.flush();
  const auto c = sink.counters();
  EXPECT_EQ(c.push_failures, 1u);
  EXPECT_EQ(c.pushed_lines, 0u);
  EXPECT_EQ(mock.requests, 3);
}

TEST(LokiPusher, ClientErrorsAreNotRetried) {
  MockLoki mock;
  mock.fail_first = 1000;
  mock.fail_status = 400;
  TelemetrySink sink(fast(mock.url()));
  const auto id = sink.addStream(sampleLabels(), summaryLabels());
  for (int t = 1; t <= 10; ++t) sink.sample(at(t, id));
  sink.flush();
  EXPECT_EQ(mock.requests, 1);
  EXPECT_EQ(sink.counters().push_failures, 1u);
}

TEST(LokiPusher, UnreachableServerNeverBlocksTheSimulationThread) {
  LokiConfig cfg = fast("http://127.0.0.1:1");
  cfg.max_attempts = 2;
  cfg.connect_timeout_ms = 100;
  TelemetrySink sink(cfg);
  const auto id = sink.addStream(sampleLabels(), summaryLabels());
  const auto t0 = std::chrono::steady_clock::now();
  for (int t = 1; t <= 3000; ++t) sink.sample(at(t, id));
  const double producer_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_LT(producer_ms, 1000.0);
  sink.flush();
  EXPECT_GT(sink.counters().push_failures, 0u);
}

TEST(LokiPusher, RingOverflowDropsAndCountsWithoutBlocking) {
  LokiConfig cfg = fast("");
  cfg.ring_capacity = 8;
  cfg.flush_interval_ms = 60'000;  // the pusher stays asleep while we overflow the ring
  TelemetrySink sink(cfg);
  const auto id = sink.addStream(sampleLabels(), summaryLabels());
  for (int t = 1; t <= 100; ++t) sink.sample(at(t, id));
  EXPECT_EQ(sink.counters().dropped_samples, 92u);
  sink.flush();
  EXPECT_EQ(sink.counters().pushed_lines, 8u);
}

TEST(LokiPusher, DestructorFlushesPendingLines) {
  MockLoki mock;
  {
    LokiConfig cfg = fast(mock.url());
    cfg.flush_interval_ms = 60'000;
    TelemetrySink sink(cfg);
    const auto id = sink.addStream(sampleLabels(), summaryLabels());
    for (int t = 1; t <= 50; ++t) sink.sample(at(t, id));
  }
  std::size_t lines = 0;
  for (const auto& b : mock.snapshot()) lines += timestamps(b).size();
  EXPECT_EQ(lines, 50u);
}

TEST(LokiPusher, NullSinkFormatsAndCountsButSendsNothing) {
  TelemetrySink sink(fast(""));
  const auto id = sink.addStream(sampleLabels(), summaryLabels());
  for (int t = 1; t <= 300; ++t) sink.sample(at(t, id));
  sink.flush();
  const auto c = sink.counters();
  EXPECT_EQ(c.pushed_lines, 300u);
  EXPECT_EQ(c.push_failures, 0u);
  EXPECT_GT(c.bytes_written, 300u * 60u);
  EXPECT_DOUBLE_EQ(c.push_ms, 0.0);
}
