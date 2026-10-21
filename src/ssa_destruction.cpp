#include "phinale/ssa_destruction.hpp"

#include <string>
#include <vector>

namespace phinale {

NaiveSSADestructor::NaiveSSADestructor(ControlFlowGraph& cfg)
    : cfg_(cfg) {
    run();
}

void NaiveSSADestructor::run() {
    if (executed_) return;
    executed_ = true;

    split_critical_edges();
    insert_copies();
    remove_phis();
}

void NaiveSSADestructor::split_critical_edges() {
    struct CriticalEdge {
        BasicBlock* pred;
        BasicBlock* succ;
    };

    std::vector<CriticalEdge> critical_edges;

    // Detect critical edges to blocks containing phi nodes:
    // An edge P -> B is critical if P has > 1 successors and B has > 1 predecessors.
    for (const auto& bb_ptr : cfg_.blocks()) {
        if (!bb_ptr || bb_ptr->phis.empty()) continue;
        BasicBlock* B = bb_ptr.get();

        if (B->predecessors().size() <= 1) continue;

        for (BasicBlock* P : B->predecessors()) {
            if (!P) continue;
            if (P->successors().size() > 1) {
                critical_edges.push_back({P, B});
            }
        }
    }

    for (const auto& [P, B] : critical_edges) {
        if (!B->label().has_value()) {
            B->set_label("bb" + std::to_string(B->id()));
        }
        std::string b_label = *B->label();

        std::string t_label = "trampoline_" + std::to_string(P->id()) + "_" + std::to_string(B->id());
        BasicBlock* T = cfg_.create_block(t_label);
        metrics_.critical_edges_split++;

        // Update control flow terminator in P targeting B to point to T
        if (!P->instructions().empty()) {
            auto& term = P->instructions().back();
            if (term.opcode == Opcode::JUMP_IF) {
                if (term.label_target.has_value() && *term.label_target == b_label) {
                    term.label_target = t_label;
                } else if (term.false_label_target.has_value() && *term.false_label_target == b_label) {
                    term.false_label_target = t_label;
                } else {
                    // Match based on successor index if label string wasn't explicitly set to b_label
                    if (P->successors().size() >= 2) {
                        if (P->successors()[0] == B && term.label_target.has_value()) {
                            term.label_target = t_label;
                        } else if (P->successors()[1] == B) {
                            term.false_label_target = t_label;
                        }
                    }
                }
            } else if (term.opcode == Opcode::JUMP) {
                if (term.label_target.has_value() && *term.label_target == b_label) {
                    term.label_target = t_label;
                } else {
                    term.label_target = t_label;
                }
            }
        }

        // T unconditionally jumps to B
        T->instructions().push_back(Instruction{
            Opcode::JUMP,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            b_label,
            std::nullopt
        });

        // Rewire edges: P -> T -> B
        P->replace_successor(B, T);
        T->add_predecessor(P);
        T->add_successor(B);
        B->replace_predecessor(P, T);

        // Update incoming references in B's phi nodes from P's ID to T's ID
        for (auto& phi : B->phis) {
            for (auto& incoming_pair : phi.incoming) {
                if (incoming_pair.first == P->id()) {
                    incoming_pair.first = T->id();
                }
            }
        }
    }
}

void NaiveSSADestructor::insert_copies() {
    for (const auto& bb_ptr : cfg_.blocks()) {
        if (!bb_ptr || bb_ptr->phis.empty()) continue;
        BasicBlock* B = bb_ptr.get();

        for (const auto& phi : B->phis) {
            for (const auto& [pred_id, operand] : phi.incoming) {
                BasicBlock* P = cfg_.get_block(pred_id);
                if (!P) continue;

                Instruction copy_inst{
                    Opcode::COPY,
                    phi.dest,
                    operand,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt
                };

                if (P->is_terminated()) {
                    P->instructions().insert(P->instructions().end() - 1, copy_inst);
                } else {
                    P->instructions().push_back(copy_inst);
                }
                metrics_.copies_inserted++;
            }
        }
    }
}

void NaiveSSADestructor::remove_phis() {
    for (const auto& bb_ptr : cfg_.blocks()) {
        if (!bb_ptr) continue;
        metrics_.phi_count_eliminated += bb_ptr->phis.size();
        bb_ptr->phis.clear();
    }
}

} // namespace phinale
