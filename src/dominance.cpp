#include "phinale/dominance.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace phinale {

DominanceAnalysis::DominanceAnalysis(const ControlFlowGraph& cfg) {
    BasicBlock* entry = cfg.entry();
    if (!entry) {
        return;
    }

    // 1. Compute reverse post-order (RPO) ordering of basic blocks from the CFG entry.
    std::vector<BasicBlock*> rpo_blocks = cfg.reverse_post_order();
    if (rpo_blocks.empty()) {
        return;
    }

    std::unordered_map<BlockID, std::size_t> rpo_index;
    for (std::size_t i = 0; i < rpo_blocks.size(); ++i) {
        rpo_index[rpo_blocks[i]->id()] = i;
    }

    // 2. Implement the Cooper-Harvey-Kennedy iterative dominator algorithm.
    // For entry block: idom[entry] = entry.
    BlockID entry_id = entry->id();
    idom[entry_id] = entry_id;

    auto intersect = [&](BlockID b1, BlockID b2) -> BlockID {
        BlockID finger1 = b1;
        BlockID finger2 = b2;
        while (finger1 != finger2) {
            while (rpo_index.at(finger1) > rpo_index.at(finger2)) {
                finger1 = idom.at(finger1);
            }
            while (rpo_index.at(finger2) > rpo_index.at(finger1)) {
                finger2 = idom.at(finger2);
            }
        }
        return finger1;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        // Skip entry (index 0)
        for (std::size_t i = 1; i < rpo_blocks.size(); ++i) {
            BasicBlock* b = rpo_blocks[i];
            BlockID b_id = b->id();

            // Find first processed predecessor
            BlockID new_idom = 0;
            bool found_first = false;
            for (BasicBlock* pred : b->predecessors()) {
                if (!pred) continue;
                BlockID p_id = pred->id();
                if (idom.find(p_id) != idom.end()) {
                    new_idom = p_id;
                    found_first = true;
                    break;
                }
            }

            if (!found_first) {
                continue;
            }

            // Intersect with remaining processed predecessors
            for (BasicBlock* pred : b->predecessors()) {
                if (!pred) continue;
                BlockID p_id = pred->id();
                if (p_id != new_idom && idom.find(p_id) != idom.end()) {
                    new_idom = intersect(new_idom, p_id);
                }
            }

            auto it = idom.find(b_id);
            if (it == idom.end() || it->second != new_idom) {
                idom[b_id] = new_idom;
                changed = true;
            }
        }
    }

    // 3. Build the explicit dominator tree (parent -> children edges).
    for (BasicBlock* b : rpo_blocks) {
        dom_tree_children[b->id()] = {};
    }
    for (BasicBlock* b : rpo_blocks) {
        BlockID b_id = b->id();
        if (b_id != entry_id) {
            BlockID parent = idom.at(b_id);
            dom_tree_children[parent].push_back(b_id);
        }
    }
    for (auto& [parent, children] : dom_tree_children) {
        std::sort(children.begin(), children.end());
    }

    // 4. Calculate Dominance Frontiers (DF):
    for (BasicBlock* b : rpo_blocks) {
        dominance_frontiers[b->id()] = {};
    }
    for (BasicBlock* b : rpo_blocks) {
        std::vector<BasicBlock*> reachable_preds;
        for (BasicBlock* pred : b->predecessors()) {
            if (pred && idom.find(pred->id()) != idom.end()) {
                reachable_preds.push_back(pred);
            }
        }

        if (reachable_preds.size() >= 2) {
            BlockID b_id = b->id();
            BlockID b_idom = idom.at(b_id);
            for (BasicBlock* pred : reachable_preds) {
                BlockID runner = pred->id();
                while (runner != b_idom) {
                    dominance_frontiers[runner].insert(b_id);
                    runner = idom.at(runner);
                }
            }
        }
    }
}

bool DominanceAnalysis::dominates(BlockID a, BlockID b) const {
    if (idom.find(a) == idom.end() || idom.find(b) == idom.end()) {
        return false;
    }
    if (a == b) {
        return true;
    }
    BlockID runner = b;
    while (true) {
        auto it = idom.find(runner);
        if (it == idom.end()) {
            return false;
        }
        BlockID parent = it->second;
        if (parent == a) {
            return true;
        }
        if (parent == runner) {
            // Reached entry block without seeing a
            return false;
        }
        runner = parent;
    }
}

bool DominanceAnalysis::strictly_dominates(BlockID a, BlockID b) const {
    return a != b && dominates(a, b);
}

const std::unordered_set<BlockID>& DominanceAnalysis::get_frontier(BlockID b) const {
    auto it = dominance_frontiers.find(b);
    if (it != dominance_frontiers.end()) {
        return it->second;
    }
    static const std::unordered_set<BlockID> empty_set;
    return empty_set;
}

BlockID DominanceAnalysis::get_idom(BlockID b) const {
    auto it = idom.find(b);
    if (it != idom.end()) {
        return it->second;
    }
    throw std::out_of_range("BlockID not found in idom map: " + std::to_string(b));
}

} // namespace phinale
