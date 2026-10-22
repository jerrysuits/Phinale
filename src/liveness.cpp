#include "phinale/liveness.hpp"

namespace phinale {

LivenessAnalysis::LivenessAnalysis(const ControlFlowGraph& cfg) {
    compute_use_def(cfg);
    solve_liveness(cfg);
}

void LivenessAnalysis::compute_use_def(const ControlFlowGraph& cfg) {
    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;
        BlockID bid = bb_ptr->id();
        auto& use = use_sets_[bid];
        auto& def = def_sets_[bid];
        live_in_[bid] = {};
        live_out_[bid] = {};

        // 1. Phis in block: reading incoming operands does NOT count as use in this block
        // (phi uses are conceptually on predecessor edges). But phi destinations are definitions in this block.
        for (const auto& phi : bb_ptr->phis) {
            def.insert(phi.dest);
        }

        // 2. Scan instructions forward
        for (const auto& inst : bb_ptr->instructions()) {
            // Check uses (src1, src2)
            if (inst.src1.has_value() && inst.src1->is_vreg()) {
                VReg v = inst.src1->as_vreg();
                if (def.find(v) == def.end()) {
                    use.insert(v);
                }
            }
            if (inst.src2.has_value() && inst.src2->is_vreg()) {
                VReg v = inst.src2->as_vreg();
                if (def.find(v) == def.end()) {
                    use.insert(v);
                }
            }

            // Check def
            if (inst.dest.has_value()) {
                def.insert(*inst.dest);
            }
        }
    }
}

void LivenessAnalysis::solve_liveness(const ControlFlowGraph& cfg) {
    bool changed = true;
    while (changed) {
        changed = false;

        // Iterate over blocks (reverse order often converges faster)
        const auto& blocks = cfg.blocks();
        for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
            const auto& bb_ptr = *it;
            if (!bb_ptr) continue;
            BlockID bid = bb_ptr->id();

            // live_out[B] = union of live_in[S] for all successors S of B
            // Also account for phi uses in successor S corresponding to edge B -> S
            std::unordered_set<VReg> new_live_out;
            for (BasicBlock* succ : bb_ptr->successors()) {
                if (!succ) continue;
                BlockID succ_id = succ->id();
                const auto& succ_live_in = live_in_[succ_id];
                new_live_out.insert(succ_live_in.begin(), succ_live_in.end());

                // Account for phi operands from this predecessor block
                for (const auto& phi : succ->phis) {
                    auto incoming_op = phi.get_incoming(bid);
                    if (incoming_op.has_value() && incoming_op->is_vreg()) {
                        new_live_out.insert(incoming_op->as_vreg());
                    }
                }
            }

            // live_in[B] = use_sets[B] union (live_out[B] - def_sets[B])
            std::unordered_set<VReg> new_live_in = use_sets_[bid];
            const auto& def = def_sets_[bid];
            for (const auto& v : new_live_out) {
                if (def.find(v) == def.end()) {
                    new_live_in.insert(v);
                }
            }

            if (new_live_out != live_out_[bid]) {
                live_out_[bid] = std::move(new_live_out);
                changed = true;
            }

            if (new_live_in != live_in_[bid]) {
                live_in_[bid] = std::move(new_live_in);
                changed = true;
            }
        }
    }
}

const std::unordered_set<VReg>& LivenessAnalysis::get_live_in(BlockID b) const {
    auto it = live_in_.find(b);
    if (it != live_in_.end()) {
        return it->second;
    }
    static const std::unordered_set<VReg> empty;
    return empty;
}

const std::unordered_set<VReg>& LivenessAnalysis::get_live_out(BlockID b) const {
    auto it = live_out_.find(b);
    if (it != live_out_.end()) {
        return it->second;
    }
    static const std::unordered_set<VReg> empty;
    return empty;
}

const std::unordered_set<VReg>& LivenessAnalysis::get_use(BlockID b) const {
    auto it = use_sets_.find(b);
    if (it != use_sets_.end()) {
        return it->second;
    }
    static const std::unordered_set<VReg> empty;
    return empty;
}

const std::unordered_set<VReg>& LivenessAnalysis::get_def(BlockID b) const {
    auto it = def_sets_.find(b);
    if (it != def_sets_.end()) {
        return it->second;
    }
    static const std::unordered_set<VReg> empty;
    return empty;
}

} // namespace phinale
