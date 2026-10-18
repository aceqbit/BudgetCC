#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "cc/cc_interface.hpp"
#include "core/packet.hpp"

using namespace bcc;

TEST(CcFactory, BuildsEveryRegisteredAlgorithmByName) {
  for (const auto& n : congestionControlNames()) {
    auto cc = makeCongestionControl(n, CcConfig{});
    ASSERT_NE(cc, nullptr) << n;
    EXPECT_EQ(std::string(cc->name()), n);
    EXPECT_GT(cc->pacingRateBps(), 0.0) << n;
    EXPECT_GT(cc->inflightCapBytes(), 0u) << n;
  }
}

TEST(CcFactory, UnknownNameThrows) {
  EXPECT_THROW(makeCongestionControl("vegas", CcConfig{}), std::invalid_argument);
}

TEST(CcFactory, UnknownParameterKeyThrowsAndKnownKeyIsAccepted) {
  CcConfig cfg;
  cfg.params["iw_pkts"] = 20.0;
  EXPECT_NO_THROW(makeCongestionControl("reno", cfg));
  cfg.params["bogus"] = 1.0;
  EXPECT_THROW(makeCongestionControl("reno", cfg), std::invalid_argument);
}

TEST(CcFactory, ParameterOverridesTakeEffect) {
  CcConfig cfg;
  cfg.params["iw_pkts"] = 40.0;
  auto cc = makeCongestionControl("cubic", cfg);
  EXPECT_EQ(cc->inflightCapBytes(), 40u * kWireBytes);
}
