#include "lumacore/parallel/threaded_executor.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace lc {
namespace {

/// Joins every joinable thread on scope exit, including during stack unwinding, so a
/// failure to start thread k never leaves threads 0..k-1 running (std::terminate otherwise).
class JoinAll {
public:
    explicit JoinAll(std::vector<std::thread>& threads) noexcept : threads_(threads) {}
    ~JoinAll() {
        for (std::thread& t : threads_) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    JoinAll(const JoinAll&) = delete;
    JoinAll& operator=(const JoinAll&) = delete;

private:
    std::vector<std::thread>& threads_;
};

std::size_t resolve_thread_count(std::size_t requested) {
    if (requested == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        requested = hw == 0 ? 1 : hw;
    }
    if (requested > ThreadedExecutor::kMaxThreads) {
        throw std::invalid_argument("ThreadedExecutor: thread_count exceeds kMaxThreads");
    }
    return requested;
}

}  // namespace

ThreadedExecutor::ThreadedExecutor(std::size_t thread_count)
    : thread_count_(resolve_thread_count(thread_count)) {}

void ThreadedExecutor::run(const SimulationJob& job, double* out) const {
    const std::size_t rows = job.grid.ny();
    const std::size_t workers = std::min(thread_count_, rows);
    if (workers <= 1) {
        compute_rows(job, 0, rows, out);
        return;
    }

    // About 8 chunks per worker: small enough to balance uneven rows, large enough that
    // the shared counter is not contended.
    const std::size_t chunk = std::max<std::size_t>(1, rows / (workers * 8));
    std::atomic<std::size_t> next_row{0};

    const auto work = [&job, out, rows, chunk, &next_row]() noexcept {
        for (;;) {
            // Relaxed is enough: the counter only distributes work. Visibility of the
            // results to the caller is guaranteed by thread::join().
            const std::size_t begin = next_row.fetch_add(chunk, std::memory_order_relaxed);
            if (begin >= rows) {
                return;
            }
            compute_rows(job, begin, std::min(begin + chunk, rows), out);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    JoinAll join_all(threads);
    for (std::size_t k = 0; k + 1 < workers; ++k) {
        threads.emplace_back(work);
    }
    work();  // the calling thread is the last worker
}

}  // namespace lc
