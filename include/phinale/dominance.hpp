#pragma once

#include "phinale/cfg.hpp"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phinale {

class DominanceAnalysis {
public:
    explicit DominanceAnalysis(const ControlFlowGraph& cfg);

    bool dominates(BlockID a, BlockID b) const;
    bool strictly_dominates(BlockID a, BlockID b) const;
    const std::unordered_set<BlockID>& get_frontier(BlockID b) const;
    BlockID get_idom(BlockID b) const;

    std::unordered_map<BlockID, BlockID> idom;
    std::unordered_map<BlockID, std::vector<BlockID>> dom_tree_children;
    std::unordered_map<BlockID, std::unordered_set<BlockID>> dominance_frontiers;
};

} // namespace phinale
