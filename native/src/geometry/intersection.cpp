#include "lumacore/geometry/intersection.hpp"

#include <cmath>
#include <utility>

namespace lc {
namespace {

constexpr bool in_open_range(double t, double t_min, double t_max) noexcept {
    return t > t_min && t < t_max;  // false for NaN
}

}  // namespace

std::optional<Hit> intersect(const Ray& ray, const Plane& plane, double t_min,
                             double t_max) noexcept {
    const double denom = dot(plane.normal(), ray.direction());
    if (std::abs(denom) < kParallelEpsilon) {
        return std::nullopt;  // parallel (including rays lying in the plane)
    }
    const double t = (plane.offset() - dot(plane.normal(), ray.origin())) / denom;
    if (!in_open_range(t, t_min, t_max)) {
        return std::nullopt;
    }
    return Hit{t, ray.at(t), plane.normal()};
}

std::optional<Hit> intersect(const Ray& ray, const Sphere& sphere, double t_min,
                             double t_max) noexcept {
    // |o + t d - c|^2 = r^2 with |d| = 1  =>  t^2 + 2 h t + c = 0.
    const Vec3 oc = ray.origin() - sphere.center();
    const double h = dot(oc, ray.direction());
    const double c = length_squared(oc) - sphere.radius() * sphere.radius();
    const double discriminant = h * h - c;
    if (discriminant < 0.0) {
        return std::nullopt;
    }

    // Stable roots: q = -h - sign(h) sqrt(disc); t0 = q, t1 = c / q (product of roots = c).
    // Avoids subtracting two nearly equal numbers when |h| >> sqrt(disc).
    const double q = -h - std::copysign(std::sqrt(discriminant), h);
    double t0 = q;
    double t1 = (q != 0.0) ? c / q : 0.0;
    if (t0 > t1) {
        std::swap(t0, t1);
    }

    double t = t0;
    if (!in_open_range(t, t_min, t_max)) {
        t = t1;
        if (!in_open_range(t, t_min, t_max)) {
            return std::nullopt;
        }
    }
    const Vec3 point = ray.at(t);
    return Hit{t, point, (point - sphere.center()) / sphere.radius()};
}

std::optional<Hit> intersect(const Ray& ray, const Triangle& triangle, double t_min,
                             double t_max) noexcept {
    // Moller & Trumbore (1997): solve o + t d = a + u e1 + v e2 by Cramer's rule.
    const Vec3 p = cross(ray.direction(), triangle.edge2());
    const double det = dot(triangle.edge1(), p);
    if (std::abs(det) < kDeterminantEpsilon) {
        return std::nullopt;  // ray parallel to the triangle's plane
    }
    const double inv_det = 1.0 / det;

    const Vec3 s = ray.origin() - triangle.a();
    const double u = dot(s, p) * inv_det;
    if (u < 0.0 || u > 1.0) {
        return std::nullopt;
    }

    const Vec3 q = cross(s, triangle.edge1());
    const double v = dot(ray.direction(), q) * inv_det;
    if (v < 0.0 || u + v > 1.0) {
        return std::nullopt;
    }

    const double t = dot(triangle.edge2(), q) * inv_det;
    if (!in_open_range(t, t_min, t_max)) {
        return std::nullopt;
    }
    return Hit{t, ray.at(t), triangle.normal()};
}

}  // namespace lc
