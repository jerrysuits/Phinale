#include "phinale/cfg_builder.hpp"

#include <unordered_map>
#include <vector>

namespace phinale {

ControlFlowGraph CFGBuilder::build(const TACFunction& function) {
    ControlFlowGraph cfg;
    const auto& insts = function.instructions;
    if (insts.empty()) {
        auto* entry = cfg.create_block("entry");
        cfg.set_entry(entry);
        cfg.add_exit(entry);
        return cfg;
    }

    // Step 1: Leader identification
    // Rule 1: The first instruction is a leader.
    // Rule 2: Any target of a conditional/unconditional jump (LABEL) is a leader.
    // Rule 3: Any instruction immediately following a jump or RETURN is a leader.
    std::vector<bool> is_leader(insts.size(), false);
    is_leader[0] = true;

    for (std::size_t i = 0; i < insts.size(); ++i) {
        const auto& inst = insts[i];
        if (inst.opcode == Opcode::LABEL) {
            is_leader[i] = true;
        } else if (inst.opcode == Opcode::JUMP || inst.opcode == Opcode::JUMP_IF || inst.opcode == Opcode::RETURN) {
            if (i + 1 < insts.size()) {
                is_leader[i + 1] = true;
            }
        }
    }

    // Step 2: Basic Block Partitioning
    // For each leader, create a BasicBlock and populate its instructions up to the next leader
    std::unordered_map<std::string, BasicBlock*> label_to_block;
    std::vector<BasicBlock*> inst_to_block(insts.size(), nullptr);
    std::vector<BasicBlock*> block_list;

    BasicBlock* current_block = nullptr;
    for (std::size_t i = 0; i < insts.size(); ++i) {
        if (is_leader[i]) {
            std::optional<std::string> block_label = std::nullopt;
            if (insts[i].opcode == Opcode::LABEL && insts[i].label_target.has_value()) {
                block_label = *insts[i].label_target;
            }
            current_block = cfg.create_block(block_label);
            block_list.push_back(current_block);

            if (block_label.has_value()) {
                label_to_block[*block_label] = current_block;
            }
        }
        inst_to_block[i] = current_block;
        current_block->instructions().push_back(insts[i]);
    }

    if (!block_list.empty()) {
        cfg.set_entry(block_list.front());
    }

    // Helper to add directed edge
    auto add_edge = [](BasicBlock* from, BasicBlock* to) {
        if (!from || !to) return;
        from->add_successor(to);
        to->add_predecessor(from);
    };

    // Step 3: Edge Resolution
    for (std::size_t b = 0; b < block_list.size(); ++b) {
        BasicBlock* bb = block_list[b];
        if (bb->instructions().empty()) continue;

        const auto& last_inst = bb->instructions().back();

        if (last_inst.opcode == Opcode::RETURN) {
            cfg.add_exit(bb);
        } else if (last_inst.opcode == Opcode::JUMP) {
            if (last_inst.label_target.has_value()) {
                auto target_it = label_to_block.find(*last_inst.label_target);
                if (target_it != label_to_block.end()) {
                    add_edge(bb, target_it->second);
                }
            }
        } else if (last_inst.opcode == Opcode::JUMP_IF) {
            // Conditional jump: target label
            if (last_inst.label_target.has_value()) {
                auto target_it = label_to_block.find(*last_inst.label_target);
                if (target_it != label_to_block.end()) {
                    add_edge(bb, target_it->second);
                }
            }
            // Fall-through or false label target
            if (last_inst.false_label_target.has_value()) {
                auto false_it = label_to_block.find(*last_inst.false_label_target);
                if (false_it != label_to_block.end()) {
                    add_edge(bb, false_it->second);
                }
            } else if (b + 1 < block_list.size()) {
                add_edge(bb, block_list[b + 1]);
            }
        } else {
            // Fall-through to next block
            if (b + 1 < block_list.size()) {
                add_edge(bb, block_list[b + 1]);
            } else {
                cfg.add_exit(bb);
            }
        }
    }

    return cfg;
}

} // namespace phinale
