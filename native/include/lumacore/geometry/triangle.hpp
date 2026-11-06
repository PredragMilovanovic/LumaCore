#pragma once

#include "lumacore/math/vec3.hpp"

namespace lc {

/// Non-degenerate triangle. Edges and the unit normal are precomputed for the
/// intersection hot path. The normal follows the right-hand rule for a -> b -> c
/// (counter-clockwise when seen from the side the normal points to).
class Triangle {
public:
    /// Throws std::invalid_argument if a vertex is not finite or the triangle is
    /// degenerate (collinear or coincident vertices).
    Triangle(const Vec3& a, const Vec3& b, const Vec3& c);

    [[nodiscard]] const Vec3& a() const noexcept { return a_; }
    [[nodiscard]] const Vec3& b() const noexcept { return b_; }
    [[nodiscard]] const Vec3& c() const noexcept { return c_; }
    [[nodiscard]] const Vec3& edge1() const noexcept { return edge1_; }  ///< b - a
    [[nodiscard]] const Vec3& edge2() const noexcept { return edge2_; }  ///< c - a
    [[nodiscard]] const Vec3& normal() const noexcept { return normal_; }
    [[nodiscard]] double area() const noexcept { return 0.5 * length(cross(edge1_, edge2_)); }
    [[nodiscard]] Vec3 centroid() const noexcept { return (a_ + b_ + c_) / 3.0; }

private:
    Vec3 a_;
    Vec3 b_;
    Vec3 c_;
    Vec3 edge1_;
    Vec3 edge2_;
    Vec3 normal_;
};

}  // namespace lc
