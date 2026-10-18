#include <gtest/gtest.h>

#include <map>
#include <set>
#include <sstream>

#include "harness/cli.hpp"
#include "harness/config.hpp"

using namespace bcc;

namespace {
RunConfig good() {
  RunConfig c;
  c.algos = {"cubic"};
  return c;
}
}  // namespace

TEST(Config, ValidateAcceptsDefaultsAndRejectsBadInput) {
  EXPECT_NO_THROW(validate(good()));
  RunConfig c = good();
  c.algos.clear();
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.algos = {"nope"};
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.mode = "weird";
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.mode = "video";
  c.algos = {"cubic", "reno"};
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.mode = "fairness";
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.buffer_pkts = 0;
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.budget_ms = 0;
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.duration = 0;
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.prop_delay = 0;
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.phase = "p3";
  EXPECT_THROW(validate(c), std::invalid_argument);
  c = good();
  c.trace.clear();
  EXPECT_THROW(validate(c), std::invalid_argument);
}

TEST(Config, NormalizeTurnsMultiFlowBulkIntoFairness) {
  RunConfig c = good();
  c.algos = {"cubic", "cubic"};
  normalize(c);
  EXPECT_EQ(c.mode, "fairness");
  EXPECT_NO_THROW(validate(c));
}

TEST(Config, LabelsAndIds) {
  RunConfig c;
  c.algos = {"budgetcc", "cubic"};
  c.run_index = 42;
  c.budget_ms = 100;
  EXPECT_EQ(c.runId(), "r0042");
  EXPECT_EQ(c.algoLabel(), "budgetcc+cubic");
  EXPECT_TRUE(c.usesBudget());
  EXPECT_EQ(c.budgetLabel(), "100");
  c.algos = {"reno"};
  EXPECT_EQ(c.budgetLabel(), "none");
  EXPECT_EQ(c.bufferBytes(), 1000u * 1250u);
  EXPECT_DOUBLE_EQ(c.recoveryQstarMs(), 104.0);
}

TEST(Config, MatrixIsExactly250RunsWithTheSpecComposition) {
  const auto runs = expandMatrix("p1");
  ASSERT_EQ(runs.size(), 250u);
  std::set<std::string> ids;
  std::map<std::string, int> by_kind;
  for (const auto& r : runs) {
    ids.insert(r.runId());
    EXPECT_EQ(r.phase, "p1");
    EXPECT_EQ(r.duration, fromSec(60));
    if (r.mode == "bulk") {
      by_kind[r.usesBudget() ? "budgetcc_bulk" : "baseline_bulk"]++;
    } else {
      by_kind[r.mode]++;
    }
    if (r.mode == "video" || r.mode == "fairness") {
      EXPECT_EQ(r.buffer_pkts, 1000u);
    }
    if (r.usesBudget() && r.mode != "bulk") {
      EXPECT_DOUBLE_EQ(r.budget_ms, 150.0);
    }
  }
  EXPECT_EQ(ids.size(), 250u);  // unique, sequential run ids
  EXPECT_EQ(by_kind["baseline_bulk"], 80);
  EXPECT_EQ(by_kind["budgetcc_bulk"], 60);
  EXPECT_EQ(by_kind["video"], 50);
  EXPECT_EQ(by_kind["fairness"], 60);
  // budget sweep: 20 runs at each of D = 100, 150, 300
  std::map<double, int> sweep;
  for (const auto& r : runs)
    if (r.mode == "bulk" && r.usesBudget()) sweep[r.budget_ms]++;
  EXPECT_EQ(sweep[100.0], 20);
  EXPECT_EQ(sweep[150.0], 20);
  EXPECT_EQ(sweep[300.0], 20);
  EXPECT_EQ(allTraceNames().size(), 10u);
  for (const auto& r : runs) validate(r);
}

TEST(Config, ResolveTracePathFindsSyntheticAndReportsMisses) {
  EXPECT_NE(resolveTracePath("step_down").find("step_down.trace"), std::string::npos);
  try {
    resolveTracePath("no_such_trace");
    FAIL();
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("no_such_trace"), std::string::npos);
  }
}

TEST(Cli, RunPrintsSummaryJsonAndRejectsBadArguments) {
  std::ostringstream out, err;
  EXPECT_EQ(runMain({"budgetcc", "run", "--algo", "reno", "--trace", "step_down", "--buffer", "1000", "--duration-s", "2"},
                    out, err),
            0)
      << err.str();
  EXPECT_NE(out.str().find("\"algo\":\"reno\""), std::string::npos);
  EXPECT_NE(out.str().find("\"utilisation_pct\""), std::string::npos);

  std::ostringstream o2, e2;
  EXPECT_EQ(runMain({"budgetcc", "run", "--algo", "vegas", "--trace", "step_down", "--buffer", "100"}, o2, e2), 2);
  EXPECT_NE(e2.str().find("unknown algorithm"), std::string::npos);

  std::ostringstream o3, e3;
  EXPECT_EQ(runMain({"budgetcc", "run", "--algo", "reno", "--trace", "step_down", "--buffer", "100", "--cc-param",
                     "oops"},
                    o3, e3),
            2);
  std::ostringstream o4, e4;
  EXPECT_EQ(runMain({"budgetcc", "run", "--algo", "reno", "--trace", "missing_trace", "--buffer", "100"}, o4, e4), 1);
  std::ostringstream o5, e5;
  EXPECT_EQ(runMain({"budgetcc", "bogus"}, o5, e5), 2);
  std::ostringstream o6, e6;
  EXPECT_EQ(runMain({"budgetcc", "run", "--algo", "reno", "--trace", "step_down", "--buffer", "100", "--cc-param",
                     "iw_pkts=20", "--duration-s", "1"},
                    o6, e6),
            0)
      << e6.str();
}
