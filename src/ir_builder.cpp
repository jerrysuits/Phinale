#include "phinale/ir_builder.hpp"

#include <stdexcept>

namespace phinale {

VReg IRBuilder::next_vreg() {
    return VReg{vreg_counter_++};
}

std::string IRBuilder::next_label(const std::string& prefix) {
    return prefix + std::to_string(label_counter_++);
}

void IRBuilder::enter_scope() {
    scope_stack_.emplace_back();
}

void IRBuilder::exit_scope() {
    if (!scope_stack_.empty()) {
        scope_stack_.pop_back();
    }
}

void IRBuilder::declare_variable(const std::string& name, VReg vreg) {
    if (scope_stack_.empty()) {
        enter_scope();
    }
    scope_stack_.back()[name] = vreg;
}

VReg IRBuilder::lookup_variable(const std::string& name) {
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    throw std::runtime_error("Variable not found in IR generation: " + name);
}

void IRBuilder::emit(Instruction inst) {
    current_instructions_.push_back(std::move(inst));
}

TACProgram IRBuilder::build_program(const ProgramAST& program) {
    TACProgram tac_prog;
    for (const auto& func : program.functions) {
        tac_prog.functions.push_back(build_function(*func));
    }
    return tac_prog;
}

TACFunction IRBuilder::build_function(const FunctionAST& function) {
    TACFunction tac_func;
    tac_func.name = function.name;
    current_instructions_.clear();
    scope_stack_.clear();
    enter_scope();

    // Map parameters to new VRegs
    for (const auto& param : function.params) {
        VReg vreg = next_vreg();
        tac_func.params.push_back(vreg);
        declare_variable(param.name, vreg);
    }

    if (function.body) {
        for (const auto& stmt : function.body->statements) {
            lower_statement(*stmt);
        }
    }

    exit_scope();
    tac_func.instructions = std::move(current_instructions_);
    return tac_func;
}

Operand IRBuilder::lower_expression(const ExprNode& expr) {
    if (auto int_lit = dynamic_cast<const IntLiteralExprAST*>(&expr)) {
        return Operand::from_int(int_lit->value);
    }

    if (auto bool_lit = dynamic_cast<const BoolLiteralExprAST*>(&expr)) {
        return Operand::from_bool(bool_lit->value);
    }

    if (auto var_expr = dynamic_cast<const VariableExprAST*>(&expr)) {
        VReg v = lookup_variable(var_expr->name);
        return Operand::from_vreg(v);
    }

    if (auto unary = dynamic_cast<const UnaryExprAST*>(&expr)) {
        Operand operand = lower_expression(*unary->operand);
        VReg dest = next_vreg();

        if (unary->op == UnaryOp::Negate) {
            emit(Instruction{Opcode::NEG, dest, operand, std::nullopt, std::nullopt, std::nullopt});
        } else if (unary->op == UnaryOp::Not) {
            // Logical not: v_dest = operand == false
            emit(Instruction{Opcode::CMP_EQ, dest, operand, Operand::from_bool(false), std::nullopt, std::nullopt});
        }
        return Operand::from_vreg(dest);
    }

    if (auto binary = dynamic_cast<const BinaryExprAST*>(&expr)) {
        Operand lhs_op = lower_expression(*binary->lhs);
        Operand rhs_op = lower_expression(*binary->rhs);

        Opcode opcode;
        switch (binary->op) {
            case BinaryOp::Add: opcode = Opcode::ADD; break;
            case BinaryOp::Sub: opcode = Opcode::SUB; break;
            case BinaryOp::Mul: opcode = Opcode::MUL; break;
            case BinaryOp::Div: opcode = Opcode::DIV; break;
            case BinaryOp::Equal: opcode = Opcode::CMP_EQ; break;
            case BinaryOp::NotEqual: opcode = Opcode::CMP_NE; break;
            case BinaryOp::Less: opcode = Opcode::CMP_LT; break;
            case BinaryOp::LessEqual: opcode = Opcode::CMP_LE; break;
            case BinaryOp::Greater: opcode = Opcode::CMP_GT; break;
            case BinaryOp::GreaterEqual: opcode = Opcode::CMP_GE; break;
        }

        VReg dest = next_vreg();
        emit(Instruction{opcode, dest, lhs_op, rhs_op, std::nullopt, std::nullopt});
        return Operand::from_vreg(dest);
    }

    if (auto call = dynamic_cast<const CallExprAST*>(&expr)) {
        std::vector<Operand> arg_ops;
        for (const auto& arg : call->args) {
            arg_ops.push_back(lower_expression(*arg));
        }

        for (const auto& arg_op : arg_ops) {
            emit(Instruction{Opcode::PARAM, std::nullopt, arg_op, std::nullopt, std::nullopt, std::nullopt});
        }

        VReg dest = next_vreg();
        emit(Instruction{Opcode::CALL, dest, Operand::from_int(static_cast<int64_t>(arg_ops.size())), std::nullopt, call->callee, std::nullopt});
        return Operand::from_vreg(dest);
    }

    throw std::runtime_error("Unknown expression node in IR lowering");
}

void IRBuilder::lower_statement(const StmtNode& stmt) {
    if (auto var_decl = dynamic_cast<const VarDeclStmtAST*>(&stmt)) {
        VReg vreg = next_vreg();
        declare_variable(var_decl->name, vreg);

        if (var_decl->initializer) {
            Operand init_val = lower_expression(*var_decl->initializer);
            emit(Instruction{Opcode::COPY, vreg, init_val, std::nullopt, std::nullopt, std::nullopt});
        }
        return;
    }

    if (auto assign = dynamic_cast<const AssignStmtAST*>(&stmt)) {
        VReg vreg = lookup_variable(assign->name);
        Operand val = lower_expression(*assign->value);
        emit(Instruction{Opcode::COPY, vreg, val, std::nullopt, std::nullopt, std::nullopt});
        return;
    }

    if (auto if_stmt = dynamic_cast<const IfStmtAST*>(&stmt)) {
        Operand cond = lower_expression(*if_stmt->condition);

        std::string label_true = next_label("L_then_");
        std::string label_false = next_label("L_else_");
        std::string label_end = next_label("L_endif_");

        if (if_stmt->else_branch) {
            emit(Instruction{Opcode::JUMP_IF, std::nullopt, cond, std::nullopt, label_true, label_false});

            emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_true, std::nullopt});
            if (if_stmt->then_branch) {
                lower_block(*if_stmt->then_branch);
            }
            emit(Instruction{Opcode::JUMP, std::nullopt, std::nullopt, std::nullopt, label_end, std::nullopt});

            emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_false, std::nullopt});
            if (auto else_block = dynamic_cast<const BlockStmtAST*>(if_stmt->else_branch.get())) {
                lower_block(*else_block);
            } else {
                lower_statement(*if_stmt->else_branch);
            }
            emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_end, std::nullopt});
        } else {
            // No else branch
            emit(Instruction{Opcode::JUMP_IF, std::nullopt, cond, std::nullopt, label_true, label_end});

            emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_true, std::nullopt});
            if (if_stmt->then_branch) {
                lower_block(*if_stmt->then_branch);
            }
            emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_end, std::nullopt});
        }
        return;
    }

    if (auto while_stmt = dynamic_cast<const WhileStmtAST*>(&stmt)) {
        std::string label_header = next_label("L_loop_");
        std::string label_body = next_label("L_body_");
        std::string label_exit = next_label("L_exit_");

        emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_header, std::nullopt});
        Operand cond = lower_expression(*while_stmt->condition);
        emit(Instruction{Opcode::JUMP_IF, std::nullopt, cond, std::nullopt, label_body, label_exit});

        emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_body, std::nullopt});
        if (while_stmt->body) {
            lower_block(*while_stmt->body);
        }
        emit(Instruction{Opcode::JUMP, std::nullopt, std::nullopt, std::nullopt, label_header, std::nullopt});

        emit(Instruction{Opcode::LABEL, std::nullopt, std::nullopt, std::nullopt, label_exit, std::nullopt});
        return;
    }

    if (auto ret_stmt = dynamic_cast<const ReturnStmtAST*>(&stmt)) {
        if (ret_stmt->value) {
            Operand val = lower_expression(*ret_stmt->value);
            emit(Instruction{Opcode::RETURN, std::nullopt, val, std::nullopt, std::nullopt, std::nullopt});
        } else {
            emit(Instruction{Opcode::RETURN, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
        }
        return;
    }

    if (auto expr_stmt = dynamic_cast<const ExprStmtAST*>(&stmt)) {
        lower_expression(*expr_stmt->expr);
        return;
    }

    if (auto block_stmt = dynamic_cast<const BlockStmtAST*>(&stmt)) {
        lower_block(*block_stmt);
        return;
    }

    throw std::runtime_error("Unknown statement node in IR lowering");
}

void IRBuilder::lower_block(const BlockStmtAST& block) {
    enter_scope();
    for (const auto& stmt : block.statements) {
        lower_statement(*stmt);
    }
    exit_scope();
}

} // namespace phinale
