#pragma once

#include <cstddef>

#include "lumacore/simulation/simulation.hpp"

namespace lc {

/// Multithreaded CPU executor using std::thread.
///
/// Rows are handed out in small chunks through one atomic counter (dynamic scheduling),
/// so threads that get cheap rows (few shadow rays) simply take more of them. Workers only
/// read the Scene and grid through const references and each row is written by exactly one
/// thread, so no locks are needed. Every row is computed by the same compute_rows() kernel
/// as SequentialExecutor, so results are bit-identical regardless of the thread count.
class ThreadedExecutor final : public IExecutor {
public:
    static constexpr std::size_t kMaxThreads = 1024;

    /// thread_count == 0 uses std::thread::hardware_concurrency() (or 1 if unknown).
    /// Throws std::invalid_argument if thread_count > kMaxThreads.
    explicit ThreadedExecutor(std::size_t thread_count = 0);

    /// Uses min(thread_count, rows) threads; the calling thread is one of them.
    /// May throw std::system_error if a thread cannot be started; already started threads
    /// are always joined first.
    void run(const SimulationJob& job, double* out) const override;

    [[nodiscard]] const char* name() const noexcept override { return "threaded"; }
    [[nodiscard]] std::size_t thread_count() const noexcept { return thread_count_; }

private:
    std::size_t thread_count_;
};

}  // namespace lc
