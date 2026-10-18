#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "telemetry/csv_writer.hpp"

using namespace bcc;

namespace {
std::vector<std::string> split(const std::string& line) {
  std::vector<std::string> f;
  std::stringstream ss(line);
  std::string cell;
  while (std::getline(ss, cell, ',')) f.push_back(cell);
  return f;
}
Sample sample(int t) {
  Sample s;
  s.t_ms = t;
  s.run = 42;
  s.flow = 1;
  s.rate_mbps = 8.4;
  s.cap_mbps = 9.1;
  s.q_ms = 42.0;
  s.owd_ms = 71.5;
  s.inflight_kb = 96;
  s.lost = 2;
  s.chat_mbps = 0.1 + 0.2;  // not exactly representable: must round-trip
  s.cwnd_kb = 12.25;
  s.qest_ms = 3.0;
  return s;
}
}  // namespace

TEST(CsvWriter, HeaderAndRowRoundTrip) {
  const auto path = (std::filesystem::temp_directory_path() / "bcc_test_samples.csv").string();
  {
    CsvWriter w(path);
    w.write(sample(10));
    w.write(sample(20));
    w.close();
  }
  std::ifstream in(path);
  std::string header, r1, r2, extra;
  std::getline(in, header);
  std::getline(in, r1);
  std::getline(in, r2);
  EXPECT_FALSE(std::getline(in, extra));
  EXPECT_EQ(header + "\n", csvHeader());
  const auto h = split(header);
  const auto a = split(r1);
  ASSERT_EQ(a.size(), h.size());
  EXPECT_EQ(a[0], "10");
  EXPECT_EQ(a[1], "r0042");
  EXPECT_EQ(a[2], "1");
  EXPECT_EQ(std::stod(a[3]), 8.4);
  EXPECT_EQ(std::stod(a[9]), 0.1 + 0.2);  // exact round trip
  EXPECT_EQ(split(r2)[0], "20");
  std::remove(path.c_str());
}

TEST(CsvWriter, UnwritablePathThrows) {
  EXPECT_THROW(CsvWriter("/no/such/dir/x.csv"), std::runtime_error);
}
