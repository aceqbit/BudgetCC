// Golden summaries: the regression oracle for Phase 2 engineering changes (CLAUDE.md section 6).
// Integers must match exactly, floats to a relative 1e-9. Regenerate with BCC_UPDATE_GOLDEN=1.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "harness/results.hpp"
#include "harness/runner.hpp"
#include "support/flat_json.hpp"
#include "support/trace_util.hpp"

using namespace bcc;
using bcc_test::FlatJson;

namespace {
std::string goldenPath(const std::string& name) {
  return std::string(BUDGETCC_SOURCE_DIR) + "/tests/golden/" + name + ".json";
}

void check(const std::string& name, const std::string& actual_json) {
  if (std::getenv("BCC_UPDATE_GOLDEN") != nullptr) {
    std::ofstream(goldenPath(name), std::ios::binary) << actual_json << "\n";
    GTEST_SKIP() << "golden regenerated";
  }
  std::ifstream f(goldenPath(name), std::ios::binary);
  ASSERT_TRUE(f.good()) << "missing golden " << name;
  std::stringstream ss;
  ss << f.rdbuf();
  const FlatJson want = bcc_test::parseFlatJson(ss.str());
  const FlatJson got = bcc_test::parseFlatJson(actual_json);
  ASSERT_EQ(want.size(), got.size());
  for (const auto& kv : want) {
    ASSERT_TRUE(got.count(kv.first)) << kv.first;
    const auto& w = kv.second;
    const auto& g = got.at(kv.first);
    ASSERT_EQ(w.kind, g.kind) << kv.first;
    if (w.kind == bcc_test::FlatValue::Str) {
      EXPECT_EQ(w.s, g.s) << kv.first;
    } else if (w.kind == bcc_test::FlatValue::Num) {
      if (w.is_int) {
        EXPECT_EQ(w.d, g.d) << kv.first;
      } else {
        EXPECT_LE(std::fabs(w.d - g.d), 1e-9 * std::max(1.0, std::fabs(w.d))) << kv.first;
      }
    }
  }
}
}  // namespace

TEST(Golden, CubicOnConstantLink) {
  const Trace t = Trace::parse(bcc_test::constantTrace(12.0, 60000), "const12");
  RunConfig c;
  c.algos = {"cubic"};
  c.trace = "const12";
  c.buffer_pkts = 1000;
  check("cubic_const12", runResultToJson(runOnce(c, t)));
}

TEST(Golden, BudgetCcOnStepDown) {
  const Trace t = Trace::load(resolveTracePath("step_down"));
  RunConfig c;
  c.algos = {"budgetcc"};
  c.trace = "step_down";
  c.buffer_pkts = 1000;
  c.budget_ms = 150;
  check("budgetcc_step_down", runResultToJson(runOnce(c, t)));
}

TEST(Golden, BudgetCcVersusCubicFairnessPairOnSawtooth) {
  const Trace t = Trace::load(resolveTracePath("sawtooth"));
  RunConfig c;
  c.algos = {"budgetcc", "cubic"};
  c.mode = "fairness";
  c.trace = "sawtooth";
  c.buffer_pkts = 1000;
  check("fairness_budgetcc_cubic_sawtooth", runResultToJson(runOnce(c, t)));
}
