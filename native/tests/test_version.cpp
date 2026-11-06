#include <gtest/gtest.h>

#include <string>

#include "lumacore/version.hpp"

TEST(Version, MatchesVersionString) {
    const lc::Version v = lc::version();
    const std::string expected =
        std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
    EXPECT_EQ(expected, lc::version_string());
}

TEST(Version, IsPreRelease) {
    EXPECT_EQ(lc::version().major, 0);
}
