#include "lumacore/geometry/bvh.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

#include "lumacore/geometry/intersection.hpp"

namespace lc {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double axis(const Vec3& v, int a) noexcept { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

Bounds empty_bounds() noexcept { return {{kInf, kInf, kInf}, {-kInf, -kInf, -kInf}}; }

void grow(Bounds& b, const Vec3& p) noexcept {
    b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
    b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
}

/// Pads a box so rounding in the slab test can never reject a ray that hits a triangle inside.
Bounds padded(Bounds b) noexcept {
    const double scale = std::max({std::abs(b.min.x), std::abs(b.min.y), std::abs(b.min.z),
                                   std::abs(b.max.x), std::abs(b.max.y), std::abs(b.max.z), 1.0});
    const double pad = 1e-9 * scale;
    b.min = b.min - Vec3{pad, pad, pad};
    b.max = b.max + Vec3{pad, pad, pad};
    return b;
}

/// Slab test against [0, t_max]. NaN (origin exactly on a slab plane with a zero direction
/// component) never rejects: the comparisons below keep the previous bound, so the test stays
/// conservative.
bool hits(const Bounds& b, const Vec3& origin, const Vec3& inv_dir, double t_max) noexcept {
    double t0 = 0.0, t1 = t_max;
    for (int a = 0; a < 3; ++a) {
        double lo = (axis(b.min, a) - axis(origin, a)) * axis(inv_dir, a);
        double hi = (axis(b.max, a) - axis(origin, a)) * axis(inv_dir, a);
        if (lo > hi) std::swap(lo, hi);
        if (lo > t0) t0 = lo;
        if (hi < t1) t1 = hi;
        if (t0 > t1) return false;
    }
    return true;
}

struct Item {
    Bounds bounds;
    Vec3 centroid;
    std::uint32_t index;
};

}  // namespace

TriangleBvh::TriangleBvh(std::vector<Triangle> triangles) {
    if (triangles.size() > std::numeric_limits<std::uint32_t>::max() / 2) {
        throw std::invalid_argument("TriangleBvh: too many triangles");
    }
    if (triangles.empty()) {
        return;
    }

    std::vector<Item> items;
    items.reserve(triangles.size());
    for (std::size_t k = 0; k < triangles.size(); ++k) {
        const Triangle& t = triangles[k];
        Bounds b = empty_bounds();
        grow(b, t.a());
        grow(b, t.b());
        grow(b, t.c());
        items.push_back({b, t.centroid(), static_cast<std::uint32_t>(k)});
    }

    nodes_.reserve(2 * items.size() / kLeafSize + 1);
    nodes_.push_back({});

    // Iterative build: (node index, begin, end, depth).
    struct Task {
        std::size_t node, begin, end, depth;
    };
    std::vector<Task> stack{{0, 0, items.size(), 1}};
    while (!stack.empty()) {
        const Task task = stack.back();
        stack.pop_back();
        depth_ = std::max(depth_, task.depth);

        Bounds bounds = empty_bounds(), centroids = empty_bounds();
        for (std::size_t k = task.begin; k < task.end; ++k) {
            grow(bounds, items[k].bounds.min);
            grow(bounds, items[k].bounds.max);
            grow(centroids, items[k].centroid);
        }
        Node& node = nodes_[task.node];
        node.bounds = padded(bounds);

        const Vec3 extent = centroids.max - centroids.min;
        const int split = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        const std::size_t count = task.end - task.begin;
        if (count <= kLeafSize || axis(extent, split) <= 0.0) {
            node.first = static_cast<std::uint32_t>(task.begin);
            node.count = static_cast<std::uint32_t>(count);
            continue;
        }

        const std::size_t mid = task.begin + count / 2;
        std::nth_element(items.begin() + static_cast<std::ptrdiff_t>(task.begin),
                         items.begin() + static_cast<std::ptrdiff_t>(mid),
                         items.begin() + static_cast<std::ptrdiff_t>(task.end),
                         [split](const Item& l, const Item& r) {
                             return axis(l.centroid, split) < axis(r.centroid, split);
                         });
        const std::size_t left = nodes_.size();
        nodes_.push_back({});
        nodes_.push_back({});
        nodes_[task.node].first = static_cast<std::uint32_t>(left);  // re-index: push_back may reallocate
        nodes_[task.node].count = 0;
        stack.push_back({left, task.begin, mid, task.depth + 1});
        stack.push_back({left + 1, mid, task.end, task.depth + 1});
    }

    triangles_.reserve(items.size());
    for (const Item& item : items) {
        triangles_.push_back(triangles[item.index]);
    }
}

bool TriangleBvh::any_hit(const Ray& ray, double t_max) const noexcept {
    if (nodes_.empty() || !(t_max > 0.0)) {
        return false;
    }
    const Vec3& d = ray.direction();
    const Vec3 inv{1.0 / d.x, 1.0 / d.y, 1.0 / d.z};  // ±inf for zero components is intended

    std::uint32_t stack[64];
    std::size_t top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (!hits(node.bounds, ray.origin(), inv, t_max)) {
            continue;
        }
        if (node.count > 0) {
            for (std::uint32_t k = node.first; k < node.first + node.count; ++k) {
                if (intersect(ray, triangles_[k], 0.0, t_max)) {
                    return true;
                }
            }
        } else if (top + 2 <= std::size(stack)) {
            stack[top++] = node.first;
            stack[top++] = node.first + 1;
        } else {
            // Unreachable for median splits (depth ~ log2(n)), but stay correct regardless:
            // fall back to testing every triangle.
            for (const Triangle& t : triangles_) {
                if (intersect(ray, t, 0.0, t_max)) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

}  // namespace lc
