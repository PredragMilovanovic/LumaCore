#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lumacore/geometry/triangle.hpp"
#include "lumacore/math/ray.hpp"

namespace lc {

/// Axis-aligned bounding box.
struct Bounds {
    Vec3 min;
    Vec3 max;
};

/// Bounding volume hierarchy over triangles, for fast any-hit (shadow-ray) queries.
///
/// Built once (median split along the longest axis of the triangle centroids, at most
/// kLeafSize triangles per leaf); queried from any number of threads (const, no mutation).
///
/// Exactness: node boxes are padded slightly and the box test is conservative (it may visit a
/// node it does not need, never skip one it does), and leaves call the same intersect() as the
/// brute-force loop. any_hit() therefore returns exactly what testing every triangle returns,
/// and simulation results do not change bit for bit (tested).
class TriangleBvh {
public:
    static constexpr std::size_t kLeafSize = 4;

    explicit TriangleBvh(std::vector<Triangle> triangles);

    /// True if any triangle is hit with t in (0, t_max).
    [[nodiscard]] bool any_hit(const Ray& ray, double t_max) const noexcept;

    [[nodiscard]] std::size_t triangle_count() const noexcept { return triangles_.size(); }
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }

private:
    struct Node {
        Bounds bounds;
        std::uint32_t first;  ///< leaf: first triangle; inner: index of the left child
        std::uint32_t count;  ///< leaf: number of triangles; inner: 0 (right child = first + 1)
    };

    std::vector<Triangle> triangles_;  // reordered so every leaf is a contiguous range
    std::vector<Node> nodes_;
    std::size_t depth_ = 0;
};

}  // namespace lc
