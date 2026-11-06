#pragma once

#include <gtest/gtest.h>

#include <ostream>
#include <random>

#include "lumacore/math/vec3.hpp"

namespace lc {

// Lets GoogleTest print Vec3 values in failure messages (found via ADL).
inline void PrintTo(const Vec3& v, std::ostream* os) {
    *os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
}

}  // namespace lc

namespace lc::test {

/// Deterministic random source: tests must be reproducible run to run.
class Random {
public:
    explicit Random(unsigned seed = 12345u) : engine_(seed) {}

    double uniform(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(engine_); }

    Vec3 vec(double lo, double hi) { return {uniform(lo, hi), uniform(lo, hi), uniform(lo, hi)}; }

    /// Random unit vector (rejection sampling inside the unit ball).
    Vec3 unit() {
        for (;;) {
            const Vec3 v = vec(-1.0, 1.0);
            const double l2 = length_squared(v);
            if (l2 > 1e-4 && l2 <= 1.0) {
                return v / std::sqrt(l2);
            }
        }
    }

private:
    std::mt19937 engine_;
};

}  // namespace lc::test

#define EXPECT_VEC3_NEAR(expected, actual, tol)                                     \
    do {                                                                            \
        const ::lc::Vec3 lc_e_ = (expected);                                        \
        const ::lc::Vec3 lc_a_ = (actual);                                          \
        EXPECT_NEAR(lc_e_.x, lc_a_.x, (tol)) << "x component";                      \
        EXPECT_NEAR(lc_e_.y, lc_a_.y, (tol)) << "y component";                      \
        EXPECT_NEAR(lc_e_.z, lc_a_.z, (tol)) << "z component";                      \
    } while (false)
