#pragma once

#include "phinale/cfg.hpp"
#include "phinale/dominance.hpp"
#include "phinale/tac.h"

#include <cstddef>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phinale {

class SSABuilder {
public:
    SSABuilder(ControlFlowGraph& cfg, const DominanceAnalysis& dom);

    void build();

private:
    void collect_defs();
    void place_phis();
    void rename_variables();
    void rename_block(BasicBlock* bb);
    VReg get_current_vreg(VRegID orig_id);

    ControlFlowGraph& cfg_;
    const DominanceAnalysis& dom_;
    bool built_{false};

    // Defs collection: orig_vreg -> set of BlockIDs where it is defined
    std::unordered_map<VRegID, std::unordered_set<BlockID>> defs_;
    std::unordered_set<VRegID> all_vars_;

    // Renaming state
    std::unordered_map<VRegID, std::size_t> counters_;
    std::unordered_map<VRegID, std::stack<VReg>> stacks_;
    std::size_t next_vreg_id_{0};
};

} // namespace phinale
