// Constructors (input validation) for Plane, Sphere and Triangle.
#include <cmath>
#include <stdexcept>

#include "lumacore/geometry/plane.hpp"
#include "lumacore/geometry/sphere.hpp"
#include "lumacore/geometry/triangle.hpp"

namespace lc {
namespace {

Vec3 checked_plane_normal(const Vec3& point, const Vec3& normal) {
    if (!is_finite(point)) {
        throw std::invalid_argument("Plane: point must be finite");
    }
    const std::optional<Vec3> unit = normalized(normal);
    if (!unit) {
        throw std::invalid_argument("Plane: normal must be finite and non-zero");
    }
    return *unit;
}

Vec3 checked_triangle_normal(const Vec3& a, const Vec3& b, const Vec3& c) {
    if (!is_finite(a) || !is_finite(b) || !is_finite(c)) {
        throw std::invalid_argument("Triangle: vertices must be finite");
    }
    const Vec3 n = cross(b - a, c - a);
    if (length(n) < kMinTriangleDoubleArea) {
        throw std::invalid_argument("Triangle: degenerate (collinear or coincident vertices)");
    }
    return n / length(n);
}

}  // namespace

Plane::Plane(const Vec3& point, const Vec3& normal)
    : normal_(checked_plane_normal(point, normal)), offset_(dot(normal_, point)) {}

Sphere::Sphere(const Vec3& center, double radius) : center_(center), radius_(radius) {
    if (!is_finite(center)) {
        throw std::invalid_argument("Sphere: center must be finite");
    }
    if (!std::isfinite(radius) || !(radius > 0.0)) {
        throw std::invalid_argument("Sphere: radius must be finite and > 0");
    }
}

Triangle::Triangle(const Vec3& a, const Vec3& b, const Vec3& c)
    : a_(a),
      b_(b),
      c_(c),
      edge1_(b - a),
      edge2_(c - a),
      normal_(checked_triangle_normal(a, b, c)) {}

}  // namespace lc
