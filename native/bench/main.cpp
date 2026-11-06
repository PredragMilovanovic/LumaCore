// lumacore_bench: sequential vs multithreaded execution of the office scenario.
//
// For each configuration: one warm-up run, then `reps` timed runs; reports the median and
// the minimum (executor wall time only, from SimulationResult::elapsed_ms). Every threaded
// result is compared bit-for-bit with the sequential baseline; a mismatch aborts the run.
//
// Usage: lumacore_bench [--cell <m>] [--reps <n>] [--out <file.csv>]

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../apps/common/scenarios.hpp"
#include "lumacore/parallel/sequential_executor.hpp"
#include "lumacore/parallel/threaded_executor.hpp"
#include "lumacore/version.hpp"

namespace {

struct Timing {
    double median_ms;
    double min_ms;
    std::vector<double> lux;  // result of the last run, for the equality check
};

Timing measure(const lc::SimulationJob& job, const lc::IExecutor& executor, int reps) {
    (void)lc::simulate(job, executor);  // warm-up: caches, page faults, thread start-up paths
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(reps));
    lc::SimulationResult last;
    for (int k = 0; k < reps; ++k) {
        last = lc::simulate(job, executor);
        times.push_back(last.elapsed_ms);
    }
    std::sort(times.begin(), times.end());
    const std::size_t n = times.size();
    const double median = n % 2 == 1 ? times[n / 2] : 0.5 * (times[n / 2 - 1] + times[n / 2]);
    return Timing{median, times.front(), std::move(last.lux)};
}

/// Bitwise comparison: also treats NaN ("not measured") cells as equal to themselves.
bool bit_identical(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0;
}

int run(int argc, char** argv) {
    double cell_m = 0.01;
    int reps = 7;
    std::string out_path = "results/bench.csv";
    for (int k = 1; k < argc; ++k) {
        const std::string arg = argv[k];
        if (arg == "--cell" && k + 1 < argc) {
            cell_m = std::atof(argv[++k]);
        } else if (arg == "--reps" && k + 1 < argc) {
            reps = std::atoi(argv[++k]);
        } else if (arg == "--out" && k + 1 < argc) {
            out_path = argv[++k];
        } else {
            std::cerr << "usage: lumacore_bench [--cell <m>] [--reps <n>] [--out <file.csv>]\n";
            return 2;
        }
    }
    if (reps < 1 || reps > 1000) {
        std::cerr << "--reps must be in 1..1000\n";
        return 2;
    }

    const lc::apps::Scenario sc = lc::apps::office_scenario();
    const lc::Scene scene = lc::apps::build_scene(sc);
    const lc::MeasurementGrid grid = lc::apps::workplane_grid(sc, cell_m, 0.5);
    const lc::SimulationJob job{scene, grid, lc::SimulationOptions{true}};

    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::size_t> thread_counts;
    for (const std::size_t t : {1u, 2u, 4u, 6u, 8u, 12u, 16u, 20u, 24u, 32u}) {
        if (t <= hw) thread_counts.push_back(t);
    }
    if (thread_counts.back() != hw) thread_counts.push_back(hw);

    std::cout << "LumaCore " << lc::version_string() << " benchmark - office scenario\n"
              << "  grid " << grid.nx() << " x " << grid.ny() << " = " << grid.point_count()
              << " points, " << scene.lights().size() << " lights, " << scene.occluder_count()
              << " occluder primitives, shadows on\n"
              << "  hardware threads " << hw << ", " << reps << " timed runs per row (median)\n\n";

    std::ofstream csv(out_path);
    if (!csv) {
        throw std::runtime_error("cannot open " + out_path + " for writing");
    }
    csv << "backend,threads,points,lights,occluders,median_ms,min_ms,speedup,efficiency,"
           "identical_to_sequential\n";

    const auto row = [&](const char* backend, std::size_t threads, const Timing& t,
                         double baseline_ms, bool identical) {
        const double speedup = baseline_ms / t.median_ms;
        const double efficiency = speedup / static_cast<double>(threads);
        std::cout << std::left << std::setw(12) << backend << std::right << std::setw(4)
                  << threads << std::fixed << std::setprecision(2) << std::setw(11)
                  << t.median_ms << " ms" << std::setw(9) << speedup << "x" << std::setw(8)
                  << std::setprecision(0) << efficiency * 100.0 << " %"
                  << (identical ? "   identical" : "   MISMATCH") << "\n";
        csv << backend << "," << threads << "," << grid.point_count() << ","
            << scene.lights().size() << "," << scene.occluder_count() << ","
            << std::setprecision(4) << t.median_ms << "," << t.min_ms << "," << speedup << ","
            << efficiency << "," << (identical ? 1 : 0) << "\n";
    };

    std::cout << "backend  threads     median    speedup  efficiency\n";
    lc::SequentialExecutor sequential;
    const Timing base = measure(job, sequential, reps);
    row("sequential", 1, base, base.median_ms, true);

    bool all_identical = true;
    for (const std::size_t t : thread_counts) {
        const lc::ThreadedExecutor threaded(t);
        const Timing timing = measure(job, threaded, reps);
        const bool identical = bit_identical(timing.lux, base.lux);
        all_identical = all_identical && identical;
        row("threaded", t, timing, base.median_ms, identical);
    }
    std::cout << "\n  wrote " << out_path << "\n";

    if (!all_identical) {
        std::cerr << "error: a threaded result differs from the sequential result\n";
        return 1;
    }
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
