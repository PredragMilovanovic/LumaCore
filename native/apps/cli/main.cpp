// lumacore_cli: runs a built-in lighting scenario on the work plane and writes the
// illuminance grid as CSV (consumed by tools/heatmap.py).
//
// Usage: lumacore_cli [office|single] [--cell <m>] [--border <m>] [--no-shadows]
//                    [--out <file.csv>]
//
// --border: width of the band along the walls that is excluded from the calculation
// area (default 0.5 m, the border zone used for working areas in EN 12464-1).

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "lumacore/parallel/sequential_executor.hpp"
#include "lumacore/simulation/simulation.hpp"
#include "lumacore/version.hpp"

namespace {

using lc::Vec3;

struct Box {
    Vec3 min;
    Vec3 max;
};
struct Ball {
    Vec3 center;
    double radius;
};
struct Lamp {
    Vec3 position;
    double intensity_cd;
};

/// Scenario description kept separately from lc::Scene so it can be written to the CSV
/// header (the Scene stores boxes only as triangles).
struct Scenario {
    std::string name;
    double width_m, depth_m, height_m;
    double workplane_m;
    std::vector<Lamp> lamps;
    std::vector<Box> boxes;
    std::vector<Ball> balls;
};

Scenario office_scenario() {
    // 6 m x 4 m x 3 m office, 2 x 3 luminaires at 2.8 m, work plane at 0.75 m
    // (the usual desk height for office lighting calculations).
    Scenario s{"office", 6.0, 4.0, 3.0, 0.75, {}, {}, {}};
    for (const double y : {1.0, 3.0}) {
        for (const double x : {1.0, 3.0, 5.0}) {
            s.lamps.push_back({{x, y, 2.8}, 800.0});
        }
    }
    s.boxes.push_back({{0.05, 2.6, 0.0}, {0.65, 3.95, 2.0}});  // tall cabinet by the wall
    s.boxes.push_back({{2.9, 0.2, 0.0}, {3.1, 1.6, 1.6}});     // desk partition screen
    s.balls.push_back({{4.5, 2.6, 1.9}, 0.25});                 // pendant globe (opaque)
    return s;
}

Scenario single_scenario() {
    // One 1000 cd source 2 m above the work plane: every point has a closed-form answer.
    Scenario s{"single", 4.0, 4.0, 3.0, 0.75, {}, {}, {}};
    s.lamps.push_back({{2.0, 2.0, 2.75}, 1000.0});
    return s;
}

lc::Scene build_scene(const Scenario& sc) {
    lc::Scene scene;
    scene.set_room(sc.width_m, sc.depth_m, sc.height_m);
    for (const Lamp& l : sc.lamps) scene.add_light(lc::PointLight(l.position, l.intensity_cd));
    for (const Box& b : sc.boxes) scene.add_box(b.min, b.max);
    for (const Ball& b : sc.balls) scene.add_sphere(lc::Sphere(b.center, b.radius));
    return scene;
}

void write_csv(const std::string& path, const Scenario& sc, const lc::MeasurementGrid& grid,
               const lc::SimulationResult& r, bool shadows) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open " + path + " for writing");
    }
    out << std::setprecision(10);
    out << "# lumacore," << lc::version_string() << "\n";
    out << "# scenario," << sc.name << "\n";
    out << "# shadows," << (shadows ? 1 : 0) << "\n";
    out << "# room," << sc.width_m << "," << sc.depth_m << "," << sc.height_m << "\n";
    const Vec3 g0 = grid.origin();
    const Vec3 g1 = grid.origin() + grid.u_edge() + grid.v_edge();
    out << "# grid," << grid.nx() << "," << grid.ny() << "," << g0.x << "," << g0.y << ","
        << g1.x << "," << g1.y << "," << sc.workplane_m << "\n";
    for (const Lamp& l : sc.lamps)
        out << "# light," << l.position.x << "," << l.position.y << "," << l.position.z << ","
            << l.intensity_cd << "\n";
    for (const Box& b : sc.boxes)
        out << "# box," << b.min.x << "," << b.min.y << "," << b.min.z << "," << b.max.x << ","
            << b.max.y << "," << b.max.z << "\n";
    for (const Ball& b : sc.balls)
        out << "# sphere," << b.center.x << "," << b.center.y << "," << b.center.z << ","
            << b.radius << "\n";
    out << "# stats," << r.stats.min_lux << "," << r.stats.max_lux << "," << r.stats.avg_lux
        << "," << r.stats.uniformity_u0 << "," << r.stats.valid_points << "\n";
    out << "i,j,x_m,y_m,z_m,lux\n";
    for (std::size_t j = 0; j < grid.ny(); ++j) {
        for (std::size_t i = 0; i < grid.nx(); ++i) {
            const Vec3 p = grid.point(i, j);
            out << i << "," << j << "," << p.x << "," << p.y << "," << p.z << ",";
            const double e = r.lux[grid.index(i, j)];
            if (std::isnan(e)) {
                out << "nan\n";  // inside a solid occluder: not measured
            } else {
                out << e << "\n";
            }
        }
    }
    if (!out) {
        throw std::runtime_error("write failed: " + path);
    }
}

/// Independent closed-form check for scenarios without occluders:
/// E = sum I h / (h^2 + d^2)^(3/2) for a horizontal, upward-facing plane.
double max_relative_error_vs_analytic(const Scenario& sc, const lc::MeasurementGrid& grid,
                                      const lc::SimulationResult& r) {
    double worst = 0.0;
    for (std::size_t j = 0; j < grid.ny(); ++j) {
        for (std::size_t i = 0; i < grid.nx(); ++i) {
            const Vec3 p = grid.point(i, j);
            double expected = 0.0;
            for (const Lamp& l : sc.lamps) {
                const double h = l.position.z - p.z;
                const double dx = l.position.x - p.x;
                const double dy = l.position.y - p.y;
                const double r2 = h * h + dx * dx + dy * dy;
                expected += l.intensity_cd * h / (r2 * std::sqrt(r2));
            }
            const double got = r.lux[grid.index(i, j)];
            worst = std::max(worst, std::abs(got - expected) / expected);
        }
    }
    return worst;
}

void print_usage() {
    std::cerr << "usage: lumacore_cli [office|single] [--cell <m>] [--border <m>] "
                 "[--no-shadows] [--out <file.csv>]\n";
}

int run(int argc, char** argv) {
    std::string scenario_name = "office";
    double cell_m = 0.05;
    double border_m = 0.5;
    bool shadows = true;
    std::string out_path;

    for (int k = 1; k < argc; ++k) {
        const std::string arg = argv[k];
        if (arg == "--cell" && k + 1 < argc) {
            cell_m = std::atof(argv[++k]);
        } else if (arg == "--border" && k + 1 < argc) {
            border_m = std::atof(argv[++k]);
        } else if (arg == "--no-shadows") {
            shadows = false;
        } else if (arg == "--out" && k + 1 < argc) {
            out_path = argv[++k];
        } else if (arg == "office" || arg == "single") {
            scenario_name = arg;
        } else {
            print_usage();
            return 2;
        }
    }
    if (!(cell_m >= 0.005 && cell_m <= 1.0)) {
        std::cerr << "--cell must be between 0.005 and 1.0 m\n";
        return 2;
    }
    if (out_path.empty()) {
        out_path = "results/" + scenario_name + ".csv";
    }

    const Scenario sc = scenario_name == "single" ? single_scenario() : office_scenario();
    if (!(border_m >= 0.0) || 2.0 * border_m >= std::min(sc.width_m, sc.depth_m)) {
        std::cerr << "--border must be >= 0 and leave a non-empty calculation area\n";
        return 2;
    }
    const lc::Scene scene = build_scene(sc);
    const double area_w = sc.width_m - 2.0 * border_m;
    const double area_d = sc.depth_m - 2.0 * border_m;
    const auto nx = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(area_w / cell_m)));
    const auto ny = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(area_d / cell_m)));
    const lc::MeasurementGrid grid = lc::MeasurementGrid::horizontal(
        border_m, border_m, sc.width_m - border_m, sc.depth_m - border_m, sc.workplane_m, nx, ny);

    lc::SequentialExecutor executor;
    const lc::SimulationJob job{scene, grid, lc::SimulationOptions{shadows}};
    const lc::SimulationResult r = lc::simulate(job, executor);
    write_csv(out_path, sc, grid, r, shadows);

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "LumaCore " << lc::version_string() << " - scenario '" << sc.name << "'\n"
              << "  room          " << sc.width_m << " x " << sc.depth_m << " x " << sc.height_m
              << " m, work plane at " << std::setprecision(2) << sc.workplane_m << " m\n"
              << "  grid          " << grid.nx() << " x " << grid.ny() << " = "
              << grid.point_count() << " points (" << cell_m * 100.0 << " cm cells, "
              << std::setprecision(2) << border_m << " m border zone excluded)\n"
              << std::setprecision(1)
              << "  measured      " << r.stats.valid_points << " points ("
              << grid.point_count() - r.stats.valid_points << " inside obstacles excluded)\n"
              << "  lights        " << scene.lights().size() << ", occluders "
              << scene.occluder_count() << " primitives, shadows " << (shadows ? "on" : "off")
              << "\n"
              << std::setprecision(1)
              << "  E_min         " << r.stats.min_lux << " lx\n"
              << "  E_avg         " << r.stats.avg_lux << " lx\n"
              << "  E_max         " << r.stats.max_lux << " lx\n"
              << std::setprecision(3)
              << "  U0 = Emin/Eavg " << r.stats.uniformity_u0 << "\n"
              << std::setprecision(2)
              << "  time          " << r.elapsed_ms << " ms (" << executor.name() << ")\n";
    if (sc.boxes.empty() && sc.balls.empty()) {
        std::cout << std::scientific << std::setprecision(2)
                  << "  max rel. error vs closed form: "
                  << max_relative_error_vs_analytic(sc, grid, r) << "\n";
    }
    std::cout << "  wrote         " << out_path << "\n";
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
