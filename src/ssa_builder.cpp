#include "phinale/ssa_builder.hpp"

#include <algorithm>

namespace phinale {

SSABuilder::SSABuilder(ControlFlowGraph& cfg, const DominanceAnalysis& dom)
    : cfg_(cfg), dom_(dom) {
    build();
}

void SSABuilder::build() {
    if (built_) return;
    built_ = true;

    if (!cfg_.entry()) return;

    collect_defs();
    place_phis();
    rename_variables();
}

void SSABuilder::collect_defs() {
    defs_.clear();
    all_vars_.clear();

    for (const auto& bb_ptr : cfg_.blocks()) {
        if (!bb_ptr) continue;
        // Only consider blocks reachable in dominance analysis
        if (dom_.idom.find(bb_ptr->id()) == dom_.idom.end()) continue;

        for (const auto& inst : bb_ptr->instructions()) {
            if (inst.dest.has_value()) {
                VRegID id = inst.dest->id;
                defs_[id].insert(bb_ptr->id());
                all_vars_.insert(id);
            }
            if (inst.src1.has_value() && inst.src1->is_vreg()) {
                all_vars_.insert(inst.src1->as_vreg().id);
            }
            if (inst.src2.has_value() && inst.src2->is_vreg()) {
                all_vars_.insert(inst.src2->as_vreg().id);
            }
        }
    }
}

void SSABuilder::place_phis() {
    // Only variables defined across multiple basic blocks need phi nodes
    std::vector<VRegID> sorted_vars;
    sorted_vars.reserve(defs_.size());
    for (const auto& [var, def_blocks] : defs_) {
        if (def_blocks.size() > 1) {
            sorted_vars.push_back(var);
        }
    }
    std::sort(sorted_vars.begin(), sorted_vars.end());

    for (VRegID v : sorted_vars) {
        const auto& def_blocks = defs_[v];
        std::vector<BlockID> W(def_blocks.begin(), def_blocks.end());
        std::sort(W.begin(), W.end());

        std::unordered_set<BlockID> has_phi_for_v;
        std::unordered_set<BlockID> in_def_set(def_blocks.begin(), def_blocks.end());

        while (!W.empty()) {
            BlockID X = W.back();
            W.pop_back();

            const auto& frontier = dom_.get_frontier(X);
            std::vector<BlockID> sorted_frontier(frontier.begin(), frontier.end());
            std::sort(sorted_frontier.begin(), sorted_frontier.end());

            for (BlockID Y : sorted_frontier) {
                if (has_phi_for_v.find(Y) == has_phi_for_v.end()) {
                    has_phi_for_v.insert(Y);

                    BasicBlock* block_y = cfg_.get_block(Y);
                    if (block_y) {
                        PhiNode phi;
                        phi.dest = VReg{v};
                        phi.original_var = v;
                        block_y->add_phi(std::move(phi));
                    }

                    if (in_def_set.find(Y) == in_def_set.end()) {
                        in_def_set.insert(Y);
                        W.push_back(Y);
                    }
                }
            }
        }
    }
}

VReg SSABuilder::get_current_vreg(VRegID orig_id) {
    auto& st = stacks_[orig_id];
    if (st.empty()) {
        // Fallback for live-in/parameters: create version 0
        std::size_t ver = counters_[orig_id]++;
        VReg fallback{next_vreg_id_++, ver, orig_id};
        st.push(fallback);
        return fallback;
    }
    return st.top();
}

void SSABuilder::rename_variables() {
    counters_.clear();
    stacks_.clear();
    next_vreg_id_ = 0;

    // Handle variables that are used without an explicit definition (parameters/live-ins)
    std::vector<VRegID> live_ins;
    for (VRegID v : all_vars_) {
        if (defs_.find(v) == defs_.end() || defs_[v].empty()) {
            live_ins.push_back(v);
        }
    }
    std::sort(live_ins.begin(), live_ins.end());
    for (VRegID v : live_ins) {
        std::size_t ver = counters_[v]++;
        VReg init_vreg{next_vreg_id_++, ver, v};
        stacks_[v].push(init_vreg);
    }

    if (cfg_.entry()) {
        rename_block(cfg_.entry());
    }
}

void SSABuilder::rename_block(BasicBlock* bb) {
    if (!bb) return;
    BlockID bb_id = bb->id();
    std::vector<VRegID> pushed_vars;

    // 1. Rename phi destinations in current block (push new SSA versions to stacks).
    for (auto& phi : bb->phis) {
        VRegID orig_v = phi.original_var.value_or(phi.dest.id);
        std::size_t ver = counters_[orig_v]++;
        VReg new_dest{next_vreg_id_++, ver, orig_v};
        phi.dest = new_dest;
        stacks_[orig_v].push(new_dest);
        pushed_vars.push_back(orig_v);
    }

    // 2. For each standard instruction in block:
    //    - Replace RHS uses with current top of stacks.
    //    - Replace LHS definition with newly generated SSA version (push to stack).
    for (auto& inst : bb->instructions()) {
        // Replace RHS uses
        if (inst.src1.has_value() && inst.src1->is_vreg()) {
            VRegID orig_id = inst.src1->as_vreg().original_id.value_or(inst.src1->as_vreg().id);
            inst.src1 = Operand::from_vreg(get_current_vreg(orig_id));
        }
        if (inst.src2.has_value() && inst.src2->is_vreg()) {
            VRegID orig_id = inst.src2->as_vreg().original_id.value_or(inst.src2->as_vreg().id);
            inst.src2 = Operand::from_vreg(get_current_vreg(orig_id));
        }

        // Replace LHS definition
        if (inst.dest.has_value()) {
            VRegID orig_v = inst.dest->original_id.value_or(inst.dest->id);
            std::size_t ver = counters_[orig_v]++;
            VReg new_dest{next_vreg_id_++, ver, orig_v};
            inst.dest = new_dest;
            stacks_[orig_v].push(new_dest);
            pushed_vars.push_back(orig_v);
        }
    }

    // 3. For each successor S of current block:
    //    - Fill the incoming argument for current block into S's phi nodes using top of stack.
    for (BasicBlock* succ : bb->successors()) {
        if (!succ) continue;
        for (auto& phi : succ->phis) {
            VRegID orig_v = phi.original_var.value_or(phi.dest.original_id.value_or(phi.dest.id));
            phi.add_incoming(bb_id, Operand::from_vreg(get_current_vreg(orig_v)));
        }
    }

    // 4. Recurse down children in dom_tree_children[current_block].
    auto it = dom_.dom_tree_children.find(bb_id);
    if (it != dom_.dom_tree_children.end()) {
        for (BlockID child_id : it->second) {
            BasicBlock* child = cfg_.get_block(child_id);
            if (child) {
                rename_block(child);
            }
        }
    }

    // 5. Pop all versions pushed during this block from stacks (restore scope).
    for (auto rit = pushed_vars.rbegin(); rit != pushed_vars.rend(); ++rit) {
        stacks_[*rit].pop();
    }
}

} // namespace phinale
