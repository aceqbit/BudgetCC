#include <gtest/gtest.h>

#include <limits>

#include "core/json.hpp"

using namespace bcc;

TEST(Json, ObjectBuilderProducesExpectedText) {
  JsonObject o;
  o.str("a", "x").i64("b", -3).u64("c", 18446744073709551615ULL).num("d", 1.5).boolean("e", true).null("f");
  EXPECT_EQ(o.finish(), R"({"a":"x","b":-3,"c":18446744073709551615,"d":1.5,"e":true,"f":null})");
}

TEST(Json, NonFiniteBecomesNull) {
  JsonObject o;
  o.num("n", std::numeric_limits<double>::quiet_NaN()).num("i", std::numeric_limits<double>::infinity());
  EXPECT_EQ(o.finish(), R"({"n":null,"i":null})");
}

TEST(Json, EscapesControlAndQuoteCharacters) {
  std::string s;
  appendJsonString(s, "a\"b\\c\n\t\x01");
  EXPECT_EQ(s, R"("a\"b\\c\n\t\u0001")");
}

TEST(Json, DoublesRoundTripShortest) {
  JsonObject o;
  o.num("x", 0.1).num("y", 1e21).num("z", 8.4);
  EXPECT_EQ(o.finish(), R"({"x":0.1,"y":1e+21,"z":8.4})");
}

TEST(Json, RawAndEmpty) {
  EXPECT_EQ(JsonObject().finish(), "{}");
  JsonObject o;
  o.raw("env", R"({"k":1})");
  EXPECT_EQ(o.finish(), R"({"env":{"k":1}})");
}
