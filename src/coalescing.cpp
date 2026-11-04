#include "phinale/coalescing.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace phinale {

ConservativeCoalescer::ConservativeCoalescer(ControlFlowGraph& cfg, InterferenceGraph& rig, std::size_t K)
    : cfg_(cfg), rig_(rig), K_(K) {
    run();
}

void ConservativeCoalescer::run() {
    if (executed_) return;
    executed_ = true;

    coalesce_moves();
    rewrite_cfg_and_eliminate_copies();
}

VReg ConservativeCoalescer::find_rep(VReg v) const {
    auto it = parent_.find(v);
    if (it == parent_.end() || it->second == v) {
        return v;
    }
    VReg root = find_rep(it->second);
    it->second = root;
    return root;
}

void ConservativeCoalescer::alias(VReg keep, VReg remove) {
    VReg root_keep = find_rep(keep);
    VReg root_remove = find_rep(remove);
    if (root_keep != root_remove) {
        parent_[root_remove] = root_keep;
    }
}

bool ConservativeCoalescer::can_coalesce_briggs(const InterferenceGraph& rig, VReg u, VReg v, std::size_t K) {
    if (u == v) {
        return true;
    }

    const auto& nu = rig.neighbors(u);
    const auto& nv = rig.neighbors(v);

    std::unordered_set<VReg> combined;
    for (const auto& w : nu) {
        if (w != u && w != v) {
            combined.insert(w);
        }
    }
    for (const auto& w : nv) {
        if (w != u && w != v) {
            combined.insert(w);
        }
    }

    std::size_t high_degree_count = 0;
    for (const auto& w : combined) {
        std::size_t deg = rig.degree(w);
        if (nu.find(w) != nu.end() && nv.find(w) != nv.end()) {
            deg = (deg > 0) ? (deg - 1) : 0;
        }
        if (deg >= K) {
            high_degree_count++;
        }
    }

    return high_degree_count < K;
}

bool ConservativeCoalescer::can_coalesce_briggs(VReg u, VReg v, std::size_t K) const {
    return can_coalesce_briggs(rig_, find_rep(u), find_rep(v), K);
}

void ConservativeCoalescer::coalesce_moves() {
    std::vector<CopyEdge> current_moves = rig_.moves();

    while (true) {
        bool coalesced_any = false;
        std::vector<CopyEdge> next_moves;

        for (const auto& copy : current_moves) {
            metrics_.candidate_copies_examined++;

            VReg u = find_rep(copy.src);
            VReg v = find_rep(copy.dst);

            if (u == v) {
                metrics_.redundant_self_copies_removed++;
                continue;
            }

            if (rig_.interferes(u, v)) {
                metrics_.copies_rejected_interfering++;
                next_moves.push_back(copy);
                continue;
            }

            if (!can_coalesce_briggs(u, v, K_)) {
                metrics_.copies_rejected_briggs++;
                next_moves.push_back(copy);
                continue;
            }

            // Successfully coalesce v into u
            alias(u, v);
            rig_.merge_nodes(u, v);
            metrics_.copies_coalesced++;
            coalesced_any = true;
        }

        current_moves = std::move(next_moves);
        if (!coalesced_any) {
            break;
        }
    }

    // Update rig_'s move list to retain only uncoalesced moves
    std::vector<CopyEdge> remaining_moves;
    for (const auto& move : current_moves) {
        VReg u = find_rep(move.dst);
        VReg v = find_rep(move.src);
        if (u != v) {
            CopyEdge updated{u, v};
            if (std::find(remaining_moves.begin(), remaining_moves.end(), updated) == remaining_moves.end()) {
                remaining_moves.push_back(updated);
            }
        }
    }
    rig_.set_moves(std::move(remaining_moves));
}

void ConservativeCoalescer::rewrite_cfg_and_eliminate_copies() {
    for (const auto& bb_ptr : cfg_.blocks()) {
        if (!bb_ptr) continue;

        // Rewrite phis if any remain
        for (auto& phi : bb_ptr->phis) {
            phi.dest = find_rep(phi.dest);
            for (auto& [bid, op] : phi.incoming) {
                (void)bid;
                if (op.is_vreg()) {
                    op = Operand::from_vreg(find_rep(op.as_vreg()));
                }
            }
        }

        // Rewrite instructions
        for (auto& inst : bb_ptr->instructions()) {
            if (inst.dest.has_value()) {
                inst.dest = find_rep(*inst.dest);
            }
            if (inst.src1.has_value() && inst.src1->is_vreg()) {
                inst.src1 = Operand::from_vreg(find_rep(inst.src1->as_vreg()));
            }
            if (inst.src2.has_value() && inst.src2->is_vreg()) {
                inst.src2 = Operand::from_vreg(find_rep(inst.src2->as_vreg()));
            }
        }

        // Erase any self-copies dest == src
        auto& insts = bb_ptr->instructions();
        insts.erase(
            std::remove_if(insts.begin(), insts.end(), [](const Instruction& inst) {
                return inst.opcode == Opcode::COPY &&
                       inst.dest.has_value() &&
                       inst.src1.has_value() &&
                       inst.src1->is_vreg() &&
                       (*inst.dest == inst.src1->as_vreg());
            }),
            insts.end()
        );
    }
}

CoalescingMetrics coalesce_copies(ControlFlowGraph& cfg, InterferenceGraph& rig, std::size_t K) {
    ConservativeCoalescer coalescer(cfg, rig, K);
    return coalescer.get_metrics();
}

} // namespace phinale
