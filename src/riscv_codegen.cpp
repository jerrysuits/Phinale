#include "phinale/riscv_codegen.hpp"

#include <sstream>
#include <unordered_set>

namespace phinale {

std::string RISCVCodeGenerator::generate(const ControlFlowGraph& cfg,
                                         const AllocationResult& alloc,
                                         const std::string& func_name) const {
    std::ostringstream ss;

    ss << "    .text\n";
    ss << "    .globl " << func_name << "\n";
    ss << func_name << ":\n";

    std::size_t frame_size = alloc.metrics.stack_frame_bytes;
    if (frame_size > 0) {
        ss << "    addi sp, sp, -" << frame_size << "\n";
        ss << "    sw s0, 0(sp)\n";
        ss << "    addi s0, sp, " << frame_size << "\n";
    }

    std::unordered_set<std::string> emitted_labels;

    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;

        if (bb_ptr->label().has_value() && emitted_labels.find(*bb_ptr->label()) == emitted_labels.end()) {
            bool first_is_label = !bb_ptr->instructions().empty() &&
                                  bb_ptr->instructions().front().opcode == Opcode::LABEL &&
                                  bb_ptr->instructions().front().label_target.has_value() &&
                                  *bb_ptr->instructions().front().label_target == *bb_ptr->label();
            if (!first_is_label) {
                ss << *bb_ptr->label() << ":\n";
                emitted_labels.insert(*bb_ptr->label());
            }
        }

        for (const auto& inst : bb_ptr->instructions()) {
            if (inst.opcode == Opcode::LABEL && inst.label_target.has_value()) {
                if (emitted_labels.find(*inst.label_target) == emitted_labels.end()) {
                    ss << *inst.label_target << ":\n";
                    emitted_labels.insert(*inst.label_target);
                }
                continue;
            }

            // Resolve operands and handle reloads if spilled
            std::string rs1 = "";
            if (inst.src1.has_value() && inst.src1->is_vreg()) {
                VReg v = inst.src1->as_vreg();
                auto it = alloc.color_map.find(v);
                if (it != alloc.color_map.end() && it->second >= 0) {
                    rs1 = ChaitinBriggsAllocator::physical_reg_name(it->second);
                } else {
                    rs1 = "t4";
                    int offset = alloc.spill_offset_map.at(v);
                    ss << "    lw " << rs1 << ", " << offset << "(s0)\n";
                }
            }

            std::string rs2 = "";
            if (inst.src2.has_value() && inst.src2->is_vreg()) {
                VReg v = inst.src2->as_vreg();
                auto it = alloc.color_map.find(v);
                if (it != alloc.color_map.end() && it->second >= 0) {
                    rs2 = ChaitinBriggsAllocator::physical_reg_name(it->second);
                } else {
                    rs2 = "t5";
                    int offset = alloc.spill_offset_map.at(v);
                    ss << "    lw " << rs2 << ", " << offset << "(s0)\n";
                }
            }

            std::string rd = "";
            bool rd_spilled = false;
            int rd_offset = 0;
            if (inst.dest.has_value()) {
                VReg v = *inst.dest;
                auto it = alloc.color_map.find(v);
                if (it != alloc.color_map.end() && it->second >= 0) {
                    rd = ChaitinBriggsAllocator::physical_reg_name(it->second);
                } else {
                    rd = "t6";
                    rd_spilled = true;
                    rd_offset = alloc.spill_offset_map.at(v);
                }
            }

            switch (inst.opcode) {
                case Opcode::COPY: {
                    if (inst.src1.has_value()) {
                        if (inst.src1->is_int()) {
                            ss << "    li " << rd << ", " << inst.src1->as_int() << "\n";
                        } else if (inst.src1->is_bool()) {
                            ss << "    li " << rd << ", " << (inst.src1->as_bool() ? 1 : 0) << "\n";
                        } else if (inst.src1->is_vreg()) {
                            if (rd != rs1) {
                                ss << "    mv " << rd << ", " << rs1 << "\n";
                            }
                        } else if (inst.src1->is_label()) {
                            ss << "    la " << rd << ", " << inst.src1->as_label() << "\n";
                        }
                    }
                    break;
                }

                case Opcode::ADD: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        int64_t imm = inst.src2->as_int();
                        if (imm >= -2048 && imm <= 2047) {
                            ss << "    addi " << rd << ", " << rs1 << ", " << imm << "\n";
                        } else {
                            ss << "    li t5, " << imm << "\n";
                            ss << "    add " << rd << ", " << rs1 << ", t5\n";
                        }
                    } else if (inst.src1.has_value() && inst.src1->is_int()) {
                        int64_t imm = inst.src1->as_int();
                        if (imm >= -2048 && imm <= 2047) {
                            ss << "    addi " << rd << ", " << rs2 << ", " << imm << "\n";
                        } else {
                            ss << "    li t4, " << imm << "\n";
                            ss << "    add " << rd << ", " << rs2 << ", t4\n";
                        }
                    } else {
                        ss << "    add " << rd << ", " << rs1 << ", " << rs2 << "\n";
                    }
                    break;
                }

                case Opcode::SUB: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        int64_t imm = inst.src2->as_int();
                        if (-imm >= -2048 && -imm <= 2047) {
                            ss << "    addi " << rd << ", " << rs1 << ", " << -imm << "\n";
                        } else {
                            ss << "    li t5, " << imm << "\n";
                            ss << "    sub " << rd << ", " << rs1 << ", t5\n";
                        }
                    } else if (inst.src1.has_value() && inst.src1->is_int()) {
                        ss << "    li t4, " << inst.src1->as_int() << "\n";
                        ss << "    sub " << rd << ", t4, " << rs2 << "\n";
                    } else {
                        ss << "    sub " << rd << ", " << rs1 << ", " << rs2 << "\n";
                    }
                    break;
                }

                case Opcode::MUL: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        ss << "    li t5, " << inst.src2->as_int() << "\n";
                        ss << "    mul " << rd << ", " << rs1 << ", t5\n";
                    } else if (inst.src1.has_value() && inst.src1->is_int()) {
                        ss << "    li t4, " << inst.src1->as_int() << "\n";
                        ss << "    mul " << rd << ", " << rs2 << ", t4\n";
                    } else {
                        ss << "    mul " << rd << ", " << rs1 << ", " << rs2 << "\n";
                    }
                    break;
                }

                case Opcode::DIV: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        ss << "    li t5, " << inst.src2->as_int() << "\n";
                        ss << "    div " << rd << ", " << rs1 << ", t5\n";
                    } else if (inst.src1.has_value() && inst.src1->is_int()) {
                        ss << "    li t4, " << inst.src1->as_int() << "\n";
                        ss << "    div " << rd << ", t4, " << rs2 << "\n";
                    } else {
                        ss << "    div " << rd << ", " << rs1 << ", " << rs2 << "\n";
                    }
                    break;
                }

                case Opcode::NEG: {
                    ss << "    neg " << rd << ", " << rs1 << "\n";
                    break;
                }

                case Opcode::CMP_LT: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        int64_t imm = inst.src2->as_int();
                        if (imm >= -2048 && imm <= 2047) {
                            ss << "    slti " << rd << ", " << rs1 << ", " << imm << "\n";
                        } else {
                            ss << "    li t5, " << imm << "\n";
                            ss << "    slt " << rd << ", " << rs1 << ", t5\n";
                        }
                    } else if (inst.src1.has_value() && inst.src1->is_int()) {
                        ss << "    li t4, " << inst.src1->as_int() << "\n";
                        ss << "    slt " << rd << ", t4, " << rs2 << "\n";
                    } else {
                        ss << "    slt " << rd << ", " << rs1 << ", " << rs2 << "\n";
                    }
                    break;
                }

                case Opcode::CMP_GT: {
                    if (inst.src1.has_value() && inst.src1->is_int()) {
                        int64_t imm = inst.src1->as_int();
                        if (imm >= -2048 && imm <= 2047) {
                            ss << "    slti " << rd << ", " << rs2 << ", " << imm << "\n";
                        } else {
                            ss << "    li t4, " << imm << "\n";
                            ss << "    slt " << rd << ", " << rs2 << ", t4\n";
                        }
                    } else if (inst.src2.has_value() && inst.src2->is_int()) {
                        ss << "    li t5, " << inst.src2->as_int() << "\n";
                        ss << "    slt " << rd << ", t5, " << rs1 << "\n";
                    } else {
                        ss << "    slt " << rd << ", " << rs2 << ", " << rs1 << "\n";
                    }
                    break;
                }

                case Opcode::CMP_LE: {
                    if (inst.src1.has_value() && inst.src1->is_int()) {
                        ss << "    li t4, " << inst.src1->as_int() << "\n";
                        ss << "    slt " << rd << ", " << rs2 << ", t4\n";
                    } else if (inst.src2.has_value() && inst.src2->is_int()) {
                        int64_t imm = inst.src2->as_int();
                        if (imm >= -2048 && imm <= 2047) {
                            ss << "    slti " << rd << ", " << rs1 << ", " << imm + 1 << "\n";
                            break;
                        } else {
                            ss << "    li t5, " << imm << "\n";
                            ss << "    slt " << rd << ", t5, " << rs1 << "\n";
                        }
                    } else {
                        ss << "    slt " << rd << ", " << rs2 << ", " << rs1 << "\n";
                    }
                    ss << "    xori " << rd << ", " << rd << ", 1\n";
                    break;
                }

                case Opcode::CMP_GE: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        int64_t imm = inst.src2->as_int();
                        if (imm >= -2048 && imm <= 2047) {
                            ss << "    slti " << rd << ", " << rs1 << ", " << imm << "\n";
                        } else {
                            ss << "    li t5, " << imm << "\n";
                            ss << "    slt " << rd << ", " << rs1 << ", t5\n";
                        }
                    } else {
                        ss << "    slt " << rd << ", " << rs1 << ", " << rs2 << "\n";
                    }
                    ss << "    xori " << rd << ", " << rd << ", 1\n";
                    break;
                }

                case Opcode::CMP_EQ: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        int64_t imm = inst.src2->as_int();
                        if (imm == 0) {
                            ss << "    seqz " << rd << ", " << rs1 << "\n";
                        } else if (-imm >= -2048 && -imm <= 2047) {
                            ss << "    addi " << rd << ", " << rs1 << ", " << -imm << "\n";
                            ss << "    seqz " << rd << ", " << rd << "\n";
                        } else {
                            ss << "    li t5, " << imm << "\n";
                            ss << "    sub " << rd << ", " << rs1 << ", t5\n";
                            ss << "    seqz " << rd << ", " << rd << "\n";
                        }
                    } else {
                        ss << "    sub " << rd << ", " << rs1 << ", " << rs2 << "\n";
                        ss << "    seqz " << rd << ", " << rd << "\n";
                    }
                    break;
                }

                case Opcode::CMP_NE: {
                    if (inst.src2.has_value() && inst.src2->is_int()) {
                        int64_t imm = inst.src2->as_int();
                        if (imm == 0) {
                            ss << "    snez " << rd << ", " << rs1 << "\n";
                        } else if (-imm >= -2048 && -imm <= 2047) {
                            ss << "    addi " << rd << ", " << rs1 << ", " << -imm << "\n";
                            ss << "    snez " << rd << ", " << rd << "\n";
                        } else {
                            ss << "    li t5, " << imm << "\n";
                            ss << "    sub " << rd << ", " << rs1 << ", t5\n";
                            ss << "    snez " << rd << ", " << rd << "\n";
                        }
                    } else {
                        ss << "    sub " << rd << ", " << rs1 << ", " << rs2 << "\n";
                        ss << "    snez " << rd << ", " << rd << "\n";
                    }
                    break;
                }

                case Opcode::JUMP: {
                    if (inst.label_target.has_value()) {
                        ss << "    j " << *inst.label_target << "\n";
                    }
                    break;
                }

                case Opcode::JUMP_IF: {
                    if (inst.label_target.has_value()) {
                        ss << "    bnez " << rs1 << ", " << *inst.label_target << "\n";
                    }
                    if (inst.false_label_target.has_value()) {
                        ss << "    j " << *inst.false_label_target << "\n";
                    }
                    break;
                }

                case Opcode::CALL: {
                    if (inst.label_target.has_value()) {
                        ss << "    call " << *inst.label_target << "\n";
                    }
                    if (inst.dest.has_value()) {
                        if (rd != "a0") {
                            ss << "    mv " << rd << ", a0\n";
                        }
                    }
                    break;
                }

                case Opcode::RETURN: {
                    if (inst.src1.has_value()) {
                        if (inst.src1->is_vreg()) {
                            if (rs1 != "a0") {
                                ss << "    mv a0, " << rs1 << "\n";
                            }
                        } else if (inst.src1->is_int()) {
                            ss << "    li a0, " << inst.src1->as_int() << "\n";
                        } else if (inst.src1->is_bool()) {
                            ss << "    li a0, " << (inst.src1->as_bool() ? 1 : 0) << "\n";
                        }
                    }
                    if (frame_size > 0) {
                        ss << "    lw s0, 0(sp)\n";
                        ss << "    addi sp, sp, " << frame_size << "\n";
                    }
                    ss << "    ret\n";
                    break;
                }

                default:
                    break;
            }

            // If destination was spilled, emit store
            if (rd_spilled) {
                ss << "    sw " << rd << ", " << rd_offset << "(s0)\n";
            }
        }
    }

    // Default return if no explicit return at exit
    bool has_return = false;
    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;
        if (!bb_ptr->instructions().empty() && bb_ptr->instructions().back().opcode == Opcode::RETURN) {
            has_return = true;
            break;
        }
    }
    if (!has_return) {
        if (frame_size > 0) {
            ss << "    lw s0, 0(sp)\n";
            ss << "    addi sp, sp, " << frame_size << "\n";
        }
        ss << "    ret\n";
    }

    return ss.str();
}

} // namespace phinale
