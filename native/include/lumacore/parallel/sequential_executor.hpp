#pragma once

#include "lumacore/simulation/simulation.hpp"

namespace lc {

/// Reference executor: all rows on the calling thread. Every other executor is
/// tested against this one.
class SequentialExecutor final : public IExecutor {
public:
    void run(const SimulationJob& job, double* out) const override {
        compute_rows(job, 0, job.grid.ny(), out);
    }
    [[nodiscard]] const char* name() const noexcept override { return "sequential"; }
};

}  // namespace lc
