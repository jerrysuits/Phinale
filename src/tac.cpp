#include "phinale/tac.h"

#include <sstream>

namespace phinale {

std::string to_string(const Operand& op) {
    if (op.is_vreg()) {
        return "v" + std::to_string(op.as_vreg().id);
    }
    if (op.is_int()) {
        return std::to_string(op.as_int());
    }
    if (op.is_bool()) {
        return op.as_bool() ? "true" : "false";
    }
    if (op.is_label()) {
        return op.as_label();
    }
    return "?";
}

std::string to_string(const PhiNode& phi) {
    std::ostringstream ss;
    ss << "v" << phi.dest.id << " = phi";
    for (std::size_t i = 0; i < phi.incoming.size(); ++i) {
        if (i > 0) ss << ",";
        ss << " [B" << phi.incoming[i].first << ": " << to_string(phi.incoming[i].second) << "]";
    }
    return ss.str();
}

static std::string op_symbol(Opcode op) {
    switch (op) {
        case Opcode::ADD: return "+";
        case Opcode::SUB: return "-";
        case Opcode::MUL: return "*";
        case Opcode::DIV: return "/";
        case Opcode::NEG: return "-";
        case Opcode::CMP_EQ: return "==";
        case Opcode::CMP_NE: return "!=";
        case Opcode::CMP_LT: return "<";
        case Opcode::CMP_LE: return "<=";
        case Opcode::CMP_GT: return ">";
        case Opcode::CMP_GE: return ">=";
        default: return "";
    }
}

std::string to_string(const Instruction& inst) {
    std::ostringstream ss;
    switch (inst.opcode) {
        case Opcode::LABEL:
            ss << (inst.label_target.has_value() ? *inst.label_target : "?") << ":";
            break;

        case Opcode::ADD:
        case Opcode::SUB:
        case Opcode::MUL:
        case Opcode::DIV:
        case Opcode::CMP_EQ:
        case Opcode::CMP_NE:
        case Opcode::CMP_LT:
        case Opcode::CMP_LE:
        case Opcode::CMP_GT:
        case Opcode::CMP_GE:
            ss << "  v" << inst.dest->id << " = ";
            if (inst.src1) ss << to_string(*inst.src1);
            ss << " " << op_symbol(inst.opcode) << " ";
            if (inst.src2) ss << to_string(*inst.src2);
            break;

        case Opcode::NEG:
            ss << "  v" << inst.dest->id << " = -";
            if (inst.src1) ss << to_string(*inst.src1);
            break;

        case Opcode::COPY:
            ss << "  v" << inst.dest->id << " = ";
            if (inst.src1) ss << to_string(*inst.src1);
            break;

        case Opcode::JUMP:
            ss << "  goto " << (inst.label_target.has_value() ? *inst.label_target : "?");
            break;

        case Opcode::JUMP_IF:
            ss << "  if ";
            if (inst.src1) ss << to_string(*inst.src1);
            ss << " goto " << (inst.label_target.has_value() ? *inst.label_target : "?");
            if (inst.false_label_target.has_value()) {
                ss << " else goto " << *inst.false_label_target;
            }
            break;

        case Opcode::PARAM:
            ss << "  param ";
            if (inst.src1) ss << to_string(*inst.src1);
            break;

        case Opcode::CALL:
            ss << "  ";
            if (inst.dest.has_value()) {
                ss << "v" << inst.dest->id << " = ";
            }
            ss << "call " << (inst.label_target.has_value() ? *inst.label_target : "?");
            if (inst.src1) ss << ", " << to_string(*inst.src1);
            break;

        case Opcode::RETURN:
            ss << "  return";
            if (inst.src1.has_value()) {
                ss << " " << to_string(*inst.src1);
            }
            break;
    }
    return ss.str();
}

std::string to_string(const TACFunction& func) {
    std::ostringstream ss;
    ss << "fn " << func.name << "(";
    for (std::size_t i = 0; i < func.params.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << "v" << func.params[i].id;
    }
    ss << "):\\n";
    for (const auto& inst : func.instructions) {
        ss << to_string(inst) << "\\n";
    }
    return ss.str();
}

std::string to_string(const TACProgram& prog) {
    std::ostringstream ss;
    for (std::size_t i = 0; i < prog.functions.size(); ++i) {
        if (i > 0) ss << "\\n";
        ss << to_string(prog.functions[i]);
    }
    return ss.str();
}

} // namespace phinale
