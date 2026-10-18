#include <gtest/gtest.h>

#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "harness/cli.hpp"
#include "harness/matrix.hpp"
#include "harness/results.hpp"

using namespace bcc;

namespace {
MatrixOptions smallMatrix(std::size_t jobs) {
  MatrixOptions mo;
  mo.phase = "p1";
  mo.duration = fromSec(2);
  mo.traces = {"step_down", "step_up", "sawtooth"};  // synthetic traces ship with the repository
  mo.mode = "bulk";
  mo.buffer = 1000;
  mo.jobs = jobs;
  return mo;
}

std::string entries(const MatrixResult& m) {
  std::string s;
  for (const RunResult& r : m.runs) s += runResultToJson(r) + "\n";
  return s;
}
}  // namespace

TEST(ParallelMatrix, ResultsAreByteIdenticalToSerialAndInRunOrder) {
  const MatrixResult serial = runMatrix(smallMatrix(1));
  ASSERT_EQ(serial.runs.size(), 21u);  // (4 baselines + 3 budgets) x 3 traces
  for (std::size_t jobs : {2u, 4u, 8u}) {
    const MatrixResult par = runMatrix(smallMatrix(jobs));
    EXPECT_EQ(entries(par), entries(serial)) << "jobs=" << jobs;
  }
  for (std::size_t i = 1; i < serial.runs.size(); ++i)
    EXPECT_LT(serial.runs[i - 1].cfg.run_index, serial.runs[i].cfg.run_index);
}

TEST(ParallelMatrix, OnDoneRunsOncePerRunAndNeverConcurrently) {
  MatrixOptions mo = smallMatrix(4);
  std::mutex probe;
  std::set<std::string> ids;
  bool overlapped = false;
  mo.on_done = [&](const RunConfig& c, const RunResult&) {
    if (!probe.try_lock()) {
      overlapped = true;
      return;
    }
    ids.insert(c.runId());
    std::this_thread::yield();
    probe.unlock();
  };
  runMatrix(mo);
  EXPECT_FALSE(overlapped);
  EXPECT_EQ(ids.size(), 21u);
}

TEST(ParallelMatrix, AFailingRunAbortsTheMatrixWithItsException) {
  MatrixOptions mo = smallMatrix(4);
  mo.duration = fromMs(1);  // below the 0.1 s minimum: every run is rejected
  EXPECT_THROW(runMatrix(mo), std::invalid_argument);
}

TEST(ParallelMatrix, JobsAboveTheRunCountAreClamped) {
  MatrixOptions mo = smallMatrix(64);
  mo.limit = 2;
  EXPECT_EQ(runMatrix(mo).runs.size(), 2u);
}

TEST(ParallelMatrix, CliAcceptsJobsAndRejectsZero) {
  std::ostringstream out, err;
  EXPECT_EQ(runMain({"budgetcc", "matrix", "--phase", "p1", "--out", "x.json", "--jobs", "0"}, out, err), 2);
  EXPECT_NE(err.str().find("--jobs"), std::string::npos);
}
