#include <gtest/gtest.h>

#include <clocale>
#include <random>

#include "support/flat_json.hpp"
#include "telemetry/json_writer.hpp"

using namespace bcc;

namespace {
Sample specSample() {
  Sample s;
  s.t_ms = 1230;
  s.run = 42;
  s.rate_mbps = 8.4;
  s.cap_mbps = 9.1;
  s.q_ms = 42.0;
  s.owd_ms = 71.5;
  s.inflight_kb = 96;
  s.lost = 0;
  return s;
}
}  // namespace

TEST(JsonWriter, GoldenLineEqualsTheSpecSample) {
  SampleJsonWriter w(false);
  EXPECT_EQ(w.write(specSample()),
            R"({"t_ms":1230,"run":"r0042","rate_mbps":8.4,"cap_mbps":9.1,"q_ms":42.0,"owd_ms":71.5,"inflight_kb":96,"lost":0})");
}

TEST(JsonWriter, ExtrasFollowTheMandatoryFieldsAndParse) {
  SampleJsonWriter w(true);
  Sample s = specSample();
  s.flow = 1;
  s.chat_mbps = 11.987654;
  s.cwnd_kb = 250.25;
  s.qest_ms = 3.14159;
  const std::string line = w.write(s);
  EXPECT_EQ(line,
            R"({"t_ms":1230,"run":"r0042","rate_mbps":8.4,"cap_mbps":9.1,"q_ms":42.0,"owd_ms":71.5,"inflight_kb":96,"lost":0,"flow":1,"chat_mbps":11.988,"cwnd_kb":250.2,"qest_ms":3.1})");
  const auto j = bcc_test::parseFlatJson(line);
  EXPECT_EQ(j.at("t_ms").d, 1230);
  EXPECT_EQ(j.at("run").s, "r0042");
  EXPECT_DOUBLE_EQ(j.at("q_ms").d, 42.0);
  EXPECT_EQ(j.at("flow").d, 1);
}

TEST(JsonWriter, NumbersAreLocaleIndependent) {
  const char* old = std::setlocale(LC_ALL, nullptr);
  const std::string saved = old != nullptr ? old : "C";
  for (const char* loc : {"de_DE.UTF-8", "de_DE", "German_Germany.1252", "fr_FR.UTF-8"}) {
    if (std::setlocale(LC_ALL, loc) != nullptr) break;  // use a comma-decimal locale if one is installed
  }
  SampleJsonWriter w(true);
  Sample s = specSample();
  s.rate_mbps = 1234.5;
  const std::string line = w.write(s);
  std::setlocale(LC_ALL, saved.c_str());
  EXPECT_NE(line.find("\"rate_mbps\":1234.5"), std::string::npos);
  EXPECT_EQ(line.find(','), line.find(",\"run\""));  // the first comma separates fields, none inside numbers
}

TEST(JsonWriter, EveryRandomSampleIsValidJsonAndBytesAreCounted) {
  std::mt19937_64 rng(9);
  SampleJsonWriter w(true);
  std::uint64_t total = 0;
  for (int i = 0; i < 10'000; ++i) {
    Sample s;
    s.t_ms = static_cast<std::int32_t>(rng() % 60000);
    s.run = static_cast<std::uint32_t>(rng() % 250);
    s.flow = static_cast<std::int32_t>(rng() % 2);
    s.rate_mbps = static_cast<double>(rng() % 100000) / 100.0;
    s.cap_mbps = static_cast<double>(rng() % 100000) / 100.0;
    s.q_ms = static_cast<double>(rng() % 5000000) / 1000.0;
    s.owd_ms = static_cast<double>(rng() % 5000000) / 1000.0;
    s.inflight_kb = static_cast<std::int32_t>(rng() % 3000);
    s.lost = static_cast<std::int32_t>(rng() % 50);
    s.chat_mbps = static_cast<double>(rng() % 1000) / 7.0;
    s.cwnd_kb = static_cast<double>(rng() % 100000) / 3.0;
    s.qest_ms = static_cast<double>(rng() % 100000) / 11.0;
    const std::string line = w.write(s);
    total += line.size();
    const auto j = bcc_test::parseFlatJson(line);  // throws if the text is not valid flat JSON
    ASSERT_EQ(j.size(), 12u);
    ASSERT_EQ(j.at("t_ms").d, s.t_ms);
  }
  EXPECT_EQ(w.bytesWritten(), total);
}
