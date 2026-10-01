#pragma once

#include "phinale/ast.hpp"
#include "phinale/tac.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace phinale {

class IRBuilder {
public:
    IRBuilder() = default;

    TACProgram build_program(const ProgramAST& program);
    TACFunction build_function(const FunctionAST& function);

private:
    VReg next_vreg();
    std::string next_label(const std::string& prefix = "L");

    void enter_scope();
    void exit_scope();
    void declare_variable(const std::string& name, VReg vreg);
    VReg lookup_variable(const std::string& name);

    void emit(Instruction inst);

    Operand lower_expression(const ExprNode& expr);
    void lower_statement(const StmtNode& stmt);
    void lower_block(const BlockStmtAST& block);

    std::size_t vreg_counter_{0};
    std::size_t label_counter_{0};

    std::vector<Instruction> current_instructions_;
    std::vector<std::unordered_map<std::string, VReg>> scope_stack_;
};

} // namespace phinale
