// Geometry demo: a visual sanity check of the Milestone 2 code (vectors, rays,
// ray-plane / ray-sphere / ray-triangle intersection, box meshes).
//
// Casts one camera ray per pixel, finds the nearest hit, and casts a shadow ray to a
// point light. Shading is a simple Lambert term for display only; it is NOT the
// photometric lux calculation (that is Milestone 3). Output: 24-bit BMP.
//
// Usage: lumacore_geometry_demo [output.bmp] [width] [height]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "lumacore/geometry/intersection.hpp"
#include "lumacore/geometry/primitives.hpp"
#include "lumacore/math/constants.hpp"
#include "lumacore/version.hpp"

namespace {

using lc::Vec3;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kShadowBias = 1e-6;  // metres; avoids self-shadowing ("shadow acne")

struct Color {
    double r, g, b;
};
Color operator*(const Color& c, double s) { return {c.r * s, c.g * s, c.b * s}; }
Color operator+(const Color& a, const Color& b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }

enum class Object { Floor, Box, Sphere, Triangle, Count };
const char* const kObjectNames[] = {"floor", "box", "sphere", "triangle"};

struct SceneHit {
    lc::Hit hit;
    Object object;
};

struct DemoScene {
    lc::Plane floor{Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 1.0}};
    std::vector<lc::Triangle> box = lc::box_triangles(Vec3{0.4, 0.4, 0.0}, Vec3{1.4, 1.4, 1.0});
    lc::Sphere sphere{Vec3{2.6, 1.0, 0.7}, 0.7};
    lc::Triangle triangle{Vec3{0.0, 2.6, 0.0}, Vec3{1.8, 2.9, 0.0}, Vec3{0.9, 2.75, 1.6}};
    Vec3 light{1.2, -0.8, 3.5};
};

struct Stats {
    long long primary_hits[static_cast<int>(Object::Count)] = {};
    long long sky = 0;
    long long shadow_rays = 0;
    long long occluded = 0;
};

/// Nearest hit over all objects with t in (0, t_max). Shrinking `limit` after each hit
/// means later primitives are only accepted if they are closer.
std::optional<SceneHit> closest_hit(const DemoScene& s, const lc::Ray& ray, double t_max) {
    std::optional<SceneHit> best;
    double limit = t_max;
    const auto consider = [&](const std::optional<lc::Hit>& h, Object o) {
        if (h) {
            best = SceneHit{*h, o};
            limit = h->t;
        }
    };
    consider(lc::intersect(ray, s.floor, 0.0, limit), Object::Floor);
    for (const lc::Triangle& t : s.box) {
        consider(lc::intersect(ray, t, 0.0, limit), Object::Box);
    }
    consider(lc::intersect(ray, s.sphere, 0.0, limit), Object::Sphere);
    consider(lc::intersect(ray, s.triangle, 0.0, limit), Object::Triangle);
    return best;
}

Color albedo(Object o, const Vec3& p) {
    switch (o) {
        case Object::Floor: {
            // 0.5 m checkerboard makes perspective and intersection errors easy to spot.
            const auto ix = static_cast<long long>(std::floor(p.x * 2.0));
            const auto iy = static_cast<long long>(std::floor(p.y * 2.0));
            return ((ix + iy) % 2 == 0) ? Color{0.85, 0.85, 0.82} : Color{0.55, 0.55, 0.53};
        }
        case Object::Box:
            return {0.95, 0.55, 0.15};
        case Object::Sphere:
            return {0.20, 0.45, 0.90};
        case Object::Triangle:
            return {0.25, 0.75, 0.35};
        case Object::Count:
            break;
    }
    return {1.0, 0.0, 1.0};
}

Color shade(const DemoScene& scene, const lc::Ray& ray, Stats& stats) {
    const std::optional<SceneHit> sh = closest_hit(scene, ray, kInf);
    if (!sh) {
        ++stats.sky;
        const double k = 0.5 * (ray.direction().z + 1.0);
        return Color{0.92, 0.95, 1.0} * (1.0 - k) + Color{0.55, 0.70, 0.95} * k;
    }
    ++stats.primary_hits[static_cast<int>(sh->object)];

    const Vec3& p = sh->hit.point;
    Vec3 n = sh->hit.normal;
    if (lc::dot(n, ray.direction()) > 0.0) {
        n = -n;  // face the viewer (triangles are two-sided)
    }

    const Vec3 to_light = scene.light - p;
    const double dist = lc::length(to_light);
    const Vec3 l = to_light / dist;
    const double lambert = std::max(0.0, lc::dot(n, l));

    bool lit = lambert > 0.0;
    if (lit) {
        ++stats.shadow_rays;
        const lc::Ray shadow_ray = lc::Ray::from_unit_direction(p + n * kShadowBias, l);
        if (closest_hit(scene, shadow_ray, dist - kShadowBias)) {
            lit = false;
            ++stats.occluded;
        }
    }
    const double ambient = 0.18;
    return albedo(sh->object, p) * (ambient + (1.0 - ambient) * (lit ? lambert : 0.0));
}

std::uint8_t to_byte(double linear) {
    const double srgb = std::pow(std::clamp(linear, 0.0, 1.0), 1.0 / 2.2);
    return static_cast<std::uint8_t>(std::lround(srgb * 255.0));
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

/// Uncompressed 24-bit BMP. `rgb` is top-down, row-major, 3 bytes per pixel.
bool write_bmp(const std::string& path, std::uint32_t w, std::uint32_t h,
               const std::vector<std::uint8_t>& rgb) {
    const std::uint32_t row_bytes = w * 3u;
    const std::uint32_t padding = (4u - row_bytes % 4u) % 4u;
    const std::uint32_t data_size = (row_bytes + padding) * h;

    std::vector<std::uint8_t> file;
    file.reserve(54u + data_size);
    file.push_back('B');
    file.push_back('M');
    put_u32(file, 54u + data_size);  // file size
    put_u32(file, 0u);               // reserved
    put_u32(file, 54u);              // pixel data offset
    put_u32(file, 40u);              // BITMAPINFOHEADER size
    put_u32(file, w);
    put_u32(file, h);  // positive height: rows stored bottom-up
    put_u16(file, 1u);
    put_u16(file, 24u);
    put_u32(file, 0u);  // BI_RGB
    put_u32(file, data_size);
    put_u32(file, 2835u);  // 72 dpi
    put_u32(file, 2835u);
    put_u32(file, 0u);
    put_u32(file, 0u);

    for (std::uint32_t row = h; row-- > 0;) {
        const std::size_t base = static_cast<std::size_t>(row) * row_bytes;
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = base + static_cast<std::size_t>(x) * 3u;
            file.push_back(rgb[i + 2]);  // BMP stores BGR
            file.push_back(rgb[i + 1]);
            file.push_back(rgb[i + 0]);
        }
        file.insert(file.end(), padding, 0u);
    }

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

int run(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "geometry_demo.bmp";
    const int width = argc > 2 ? std::atoi(argv[2]) : 960;
    const int height = argc > 3 ? std::atoi(argv[3]) : 600;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        std::cerr << "width and height must be in 1..8192\n";
        return 2;
    }

    const DemoScene scene;

    // Pinhole camera (right-handed, +Z up).
    const Vec3 eye{-2.6, -3.4, 2.8};
    const Vec3 target{1.4, 1.3, 0.5};
    const Vec3 forward = *lc::normalized(target - eye);
    const Vec3 right = *lc::normalized(lc::cross(forward, Vec3{0.0, 0.0, 1.0}));
    const Vec3 up = lc::cross(right, forward);
    const double tan_half_fov = std::tan(0.5 * 50.0 * lc::kPi / 180.0);
    const double aspect = static_cast<double>(width) / static_cast<double>(height);

    Stats stats;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);

    const auto start = std::chrono::steady_clock::now();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double sx = (2.0 * (x + 0.5) / width - 1.0) * tan_half_fov * aspect;
            const double sy = (1.0 - 2.0 * (y + 0.5) / height) * tan_half_fov;
            const lc::Ray ray(eye, forward + right * sx + up * sy);
            const Color c = shade(scene, ray, stats);
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 3u;
            rgb[i + 0] = to_byte(c.r);
            rgb[i + 1] = to_byte(c.g);
            rgb[i + 2] = to_byte(c.b);
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();

    if (!write_bmp(path, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                   rgb)) {
        std::cerr << "failed to write " << path << "\n";
        return 1;
    }

    const long long primary = static_cast<long long>(width) * height;
    std::cout << "LumaCore " << lc::version_string() << " geometry demo\n"
              << "  image         " << width << " x " << height << " px -> " << path << "\n"
              << "  primary rays  " << primary << "\n";
    for (int o = 0; o < static_cast<int>(Object::Count); ++o) {
        std::cout << "    hit " << kObjectNames[o] << ": " << stats.primary_hits[o] << "\n";
    }
    std::cout << "    sky: " << stats.sky << "\n"
              << "  shadow rays   " << stats.shadow_rays << " (" << stats.occluded
              << " occluded)\n"
              << "  render time   " << ms << " ms (single thread)\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
