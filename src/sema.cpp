#include "phinale/sema.hpp"

namespace phinale {

void SemanticAnalyzer::enter_scope() {
    current_scope_ = std::make_shared<Scope>(current_scope_);
}

void SemanticAnalyzer::exit_scope() {
    if (current_scope_) {
        current_scope_ = current_scope_->parent();
    }
}

void SemanticAnalyzer::analyze(const ProgramAST& program) {
    functions_.clear();

    // First pass: collect function signatures
    for (const auto& func : program.functions) {
        if (functions_.find(func->name) != functions_.end()) {
            throw SemanticError("Duplicate function definition '" + func->name + "'", func->location);
        }

        std::vector<TypeKind> param_types;
        for (const auto& p : func->params) {
            param_types.push_back(p.type);
        }

        functions_.emplace(func->name, FunctionSignature{func->name, func->return_type, std::move(param_types), func->location});
    }

    // Second pass: analyze function bodies
    for (const auto& func : program.functions) {
        analyze_function(*func);
    }
}

void SemanticAnalyzer::analyze_function(const FunctionAST& function) {
    current_function_return_type_ = function.return_type;
    enter_scope();

    // Declare parameters in function scope
    for (const auto& p : function.params) {
        Symbol sym{p.name, p.type, p.location};
        if (!current_scope_->declare(p.name, sym)) {
            throw SemanticError("Duplicate parameter name '" + p.name + "' in function '" + function.name + "'", p.location);
        }
    }

    if (function.body) {
        // The block statements will be analyzed within the scope containing the parameters
        for (const auto& stmt : function.body->statements) {
            analyze_statement(*stmt);
        }
    }

    exit_scope();
}

void SemanticAnalyzer::analyze_block(const BlockStmtAST& block) {
    enter_scope();
    for (const auto& stmt : block.statements) {
        analyze_statement(*stmt);
    }
    exit_scope();
}

void SemanticAnalyzer::analyze_statement(const StmtNode& stmt) {
    if (auto var_decl = dynamic_cast<const VarDeclStmtAST*>(&stmt)) {
        if (var_decl->initializer) {
            TypeKind init_type = analyze_expression(*var_decl->initializer);
            if (init_type != var_decl->type) {
                throw SemanticError("Type mismatch in variable initialization: expected '" +
                                    to_string(var_decl->type) + "', got '" + to_string(init_type) + "'",
                                    var_decl->location);
            }
        }

        Symbol sym{var_decl->name, var_decl->type, var_decl->location};
        if (!current_scope_->declare(var_decl->name, sym)) {
            throw SemanticError("Duplicate variable declaration '" + var_decl->name + "' in the same scope", var_decl->location);
        }
        return;
    }

    if (auto assign = dynamic_cast<const AssignStmtAST*>(&stmt)) {
        const Symbol* sym = current_scope_->resolve(assign->name);
        if (!sym) {
            throw SemanticError("Assignment to undeclared variable '" + assign->name + "'", assign->location);
        }

        TypeKind val_type = analyze_expression(*assign->value);
        if (val_type != sym->type) {
            throw SemanticError("Type mismatch in assignment to '" + assign->name + "': expected '" +
                                to_string(sym->type) + "', got '" + to_string(val_type) + "'",
                                assign->location);
        }
        return;
    }

    if (auto if_stmt = dynamic_cast<const IfStmtAST*>(&stmt)) {
        TypeKind cond_type = analyze_expression(*if_stmt->condition);
        if (cond_type != TypeKind::Bool) {
            throw SemanticError("If condition must evaluate to 'bool', got '" + to_string(cond_type) + "'",
                                if_stmt->location);
        }

        if (if_stmt->then_branch) {
            analyze_block(*if_stmt->then_branch);
        }
        if (if_stmt->else_branch) {
            if (auto else_block = dynamic_cast<const BlockStmtAST*>(if_stmt->else_branch.get())) {
                analyze_block(*else_block);
            } else {
                analyze_statement(*if_stmt->else_branch);
            }
        }
        return;
    }

    if (auto while_stmt = dynamic_cast<const WhileStmtAST*>(&stmt)) {
        TypeKind cond_type = analyze_expression(*while_stmt->condition);
        if (cond_type != TypeKind::Bool) {
            throw SemanticError("While loop condition must evaluate to 'bool', got '" + to_string(cond_type) + "'",
                                while_stmt->location);
        }

        if (while_stmt->body) {
            analyze_block(*while_stmt->body);
        }
        return;
    }

    if (auto ret_stmt = dynamic_cast<const ReturnStmtAST*>(&stmt)) {
        if (ret_stmt->value) {
            TypeKind val_type = analyze_expression(*ret_stmt->value);
            if (val_type != current_function_return_type_) {
                throw SemanticError("Return type mismatch: expected '" + to_string(current_function_return_type_) +
                                    "', got '" + to_string(val_type) + "'",
                                    ret_stmt->location);
            }
        } else {
            if (current_function_return_type_ != TypeKind::Void) {
                throw SemanticError("Non-void function must return a value of type '" +
                                    to_string(current_function_return_type_) + "'",
                                    ret_stmt->location);
            }
        }
        return;
    }

    if (auto expr_stmt = dynamic_cast<const ExprStmtAST*>(&stmt)) {
        analyze_expression(*expr_stmt->expr);
        return;
    }

    if (auto block_stmt = dynamic_cast<const BlockStmtAST*>(&stmt)) {
        analyze_block(*block_stmt);
        return;
    }

    throw SemanticError("Unknown statement type", stmt.location);
}

TypeKind SemanticAnalyzer::analyze_expression(const ExprNode& expr) {
    if (dynamic_cast<const IntLiteralExprAST*>(&expr)) {
        return TypeKind::Int;
    }

    if (dynamic_cast<const BoolLiteralExprAST*>(&expr)) {
        return TypeKind::Bool;
    }

    if (auto var_expr = dynamic_cast<const VariableExprAST*>(&expr)) {
        const Symbol* sym = current_scope_->resolve(var_expr->name);
        if (!sym) {
            throw SemanticError("Use of undeclared variable '" + var_expr->name + "'", var_expr->location);
        }
        return sym->type;
    }

    if (auto unary = dynamic_cast<const UnaryExprAST*>(&expr)) {
        TypeKind operand_type = analyze_expression(*unary->operand);
        if (unary->op == UnaryOp::Negate) {
            if (operand_type != TypeKind::Int) {
                throw SemanticError("Unary negation '-' requires operand of type 'int', got '" +
                                    to_string(operand_type) + "'",
                                    unary->location);
            }
            return TypeKind::Int;
        }
        if (unary->op == UnaryOp::Not) {
            if (operand_type != TypeKind::Bool) {
                throw SemanticError("Unary logical not requires operand of type 'bool', got '" +
                                    to_string(operand_type) + "'",
                                    unary->location);
            }
            return TypeKind::Bool;
        }
    }

    if (auto binary = dynamic_cast<const BinaryExprAST*>(&expr)) {
        TypeKind lhs_type = analyze_expression(*binary->lhs);
        TypeKind rhs_type = analyze_expression(*binary->rhs);

        switch (binary->op) {
            case BinaryOp::Add:
            case BinaryOp::Sub:
            case BinaryOp::Mul:
            case BinaryOp::Div:
                if (lhs_type != TypeKind::Int || rhs_type != TypeKind::Int) {
                    throw SemanticError("Arithmetic operators require both operands of type 'int', got '" +
                                        to_string(lhs_type) + "' and '" + to_string(rhs_type) + "'",
                                        binary->location);
                }
                return TypeKind::Int;

            case BinaryOp::Less:
            case BinaryOp::LessEqual:
            case BinaryOp::Greater:
            case BinaryOp::GreaterEqual:
                if (lhs_type != TypeKind::Int || rhs_type != TypeKind::Int) {
                    throw SemanticError("Relational operators require both operands of type 'int', got '" +
                                        to_string(lhs_type) + "' and '" + to_string(rhs_type) + "'",
                                        binary->location);
                }
                return TypeKind::Bool;

            case BinaryOp::Equal:
            case BinaryOp::NotEqual:
                if (lhs_type != rhs_type) {
                    throw SemanticError("Equality operators require operands of the same type, got '" +
                                        to_string(lhs_type) + "' and '" + to_string(rhs_type) + "'",
                                        binary->location);
                }
                return TypeKind::Bool;
        }
    }

    if (auto call = dynamic_cast<const CallExprAST*>(&expr)) {
        auto it = functions_.find(call->callee);
        if (it == functions_.end()) {
            throw SemanticError("Call to undeclared function '" + call->callee + "'", call->location);
        }

        const auto& sig = it->second;
        if (call->args.size() != sig.param_types.size()) {
            throw SemanticError("Function '" + call->callee + "' expects " +
                                std::to_string(sig.param_types.size()) + " arguments, but got " +
                                std::to_string(call->args.size()),
                                call->location);
        }

        for (std::size_t i = 0; i < call->args.size(); ++i) {
            TypeKind arg_type = analyze_expression(*call->args[i]);
            if (arg_type != sig.param_types[i]) {
                throw SemanticError("Argument " + std::to_string(i + 1) + " of function '" + call->callee +
                                    "' expects type '" + to_string(sig.param_types[i]) +
                                    "', but got '" + to_string(arg_type) + "'",
                                    call->args[i]->location);
            }
        }

        return sig.return_type;
    }

    throw SemanticError("Unknown expression type", expr.location);
}

} // namespace phinale
