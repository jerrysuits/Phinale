#pragma once

#include "phinale/cfg.hpp"
#include "phinale/tac.h"

#include <cstddef>

namespace phinale {

struct SSADestructionMetrics {
    std::size_t phi_count_eliminated = 0;
    std::size_t copies_inserted = 0;
    std::size_t critical_edges_split = 0;
};

class NaiveSSADestructor {
public:
    explicit NaiveSSADestructor(ControlFlowGraph& cfg);

    void run();
    const SSADestructionMetrics& get_metrics() const noexcept { return metrics_; }

private:
    void split_critical_edges();
    void insert_copies();
    void remove_phis();

    ControlFlowGraph& cfg_;
    SSADestructionMetrics metrics_;
    bool executed_{false};
};

} // namespace phinale
