#pragma once

#include <cmath>
#include <optional>

#include "lumacore/math/constants.hpp"

namespace lc {

/// 3D vector / point in metres. Plain value type: trivially copyable, aggregate.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// ---- Arithmetic (constexpr, noexcept) -------------------------------------

constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
constexpr Vec3 operator-(const Vec3& v) noexcept { return {-v.x, -v.y, -v.z}; }
constexpr Vec3 operator*(const Vec3& v, double s) noexcept { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator*(double s, const Vec3& v) noexcept { return v * s; }
/// Division by zero follows IEEE-754 (inf/nan); callers must guard.
constexpr Vec3 operator/(const Vec3& v, double s) noexcept { return {v.x / s, v.y / s, v.z / s}; }

constexpr Vec3& operator+=(Vec3& a, const Vec3& b) noexcept {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}
constexpr Vec3& operator-=(Vec3& a, const Vec3& b) noexcept {
    a.x -= b.x;
    a.y -= b.y;
    a.z -= b.z;
    return a;
}
constexpr Vec3& operator*=(Vec3& v, double s) noexcept {
    v.x *= s;
    v.y *= s;
    v.z *= s;
    return v;
}

/// Exact component-wise equality. Use approx_equal for computed values.
constexpr bool operator==(const Vec3& a, const Vec3& b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
constexpr bool operator!=(const Vec3& a, const Vec3& b) noexcept { return !(a == b); }

// ---- Products and norms ----------------------------------------------------

constexpr double dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// Right-handed cross product: cross(x, y) == z.
constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

constexpr double length_squared(const Vec3& v) noexcept { return dot(v, v); }

inline double length(const Vec3& v) noexcept { return std::sqrt(length_squared(v)); }

inline double distance(const Vec3& a, const Vec3& b) noexcept { return length(b - a); }

inline bool is_finite(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// Unit vector in the direction of v, or nullopt if v is (near) zero or not finite.
inline std::optional<Vec3> normalized(const Vec3& v) noexcept {
    const double len = length(v);
    if (!std::isfinite(len) || len < kMinNormalizableLength) {
        return std::nullopt;
    }
    return v / len;
}

/// Component-wise comparison with an absolute tolerance.
inline bool approx_equal(const Vec3& a, const Vec3& b, double abs_tolerance) noexcept {
    return std::abs(a.x - b.x) <= abs_tolerance && std::abs(a.y - b.y) <= abs_tolerance &&
           std::abs(a.z - b.z) <= abs_tolerance;
}

}  // namespace lc
