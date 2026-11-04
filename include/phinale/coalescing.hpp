#pragma once

#include "phinale/cfg.hpp"
#include "phinale/interference_graph.hpp"
#include "phinale/tac.h"

#include <cstddef>
#include <unordered_map>

namespace phinale {

struct CoalescingMetrics {
    std::size_t candidate_copies_examined = 0;
    std::size_t copies_coalesced = 0;
    std::size_t copies_rejected_interfering = 0;
    std::size_t copies_rejected_briggs = 0;
    std::size_t redundant_self_copies_removed = 0;
};

class ConservativeCoalescer {
public:
    explicit ConservativeCoalescer(ControlFlowGraph& cfg, InterferenceGraph& rig, std::size_t K = 27);

    void run();

    const CoalescingMetrics& get_metrics() const noexcept { return metrics_; }
    VReg find_rep(VReg v) const;
    bool can_coalesce_briggs(VReg u, VReg v, std::size_t K) const;
    static bool can_coalesce_briggs(const InterferenceGraph& rig, VReg u, VReg v, std::size_t K);

private:
    void coalesce_moves();
    void rewrite_cfg_and_eliminate_copies();
    void alias(VReg keep, VReg remove);

    ControlFlowGraph& cfg_;
    InterferenceGraph& rig_;
    std::size_t K_;
    CoalescingMetrics metrics_;
    bool executed_{false};
    mutable std::unordered_map<VReg, VReg> parent_;
};

CoalescingMetrics coalesce_copies(ControlFlowGraph& cfg, InterferenceGraph& rig, std::size_t K = 27);

} // namespace phinale
