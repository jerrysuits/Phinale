#pragma once

#include "phinale/cfg.hpp"
#include "phinale/tac.h"

#include <unordered_map>
#include <unordered_set>

namespace phinale {

class LivenessAnalysis {
public:
    explicit LivenessAnalysis(const ControlFlowGraph& cfg);

    const std::unordered_set<VReg>& get_live_in(BlockID b) const;
    const std::unordered_set<VReg>& get_live_out(BlockID b) const;
    const std::unordered_set<VReg>& get_use(BlockID b) const;
    const std::unordered_set<VReg>& get_def(BlockID b) const;

private:
    void compute_use_def(const ControlFlowGraph& cfg);
    void solve_liveness(const ControlFlowGraph& cfg);

    std::unordered_map<BlockID, std::unordered_set<VReg>> use_sets_;
    std::unordered_map<BlockID, std::unordered_set<VReg>> def_sets_;
    std::unordered_map<BlockID, std::unordered_set<VReg>> live_in_;
    std::unordered_map<BlockID, std::unordered_set<VReg>> live_out_;
};

} // namespace phinale
