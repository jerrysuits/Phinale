#pragma once

#include "phinale/ast.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace phinale {

class SemanticError : public std::runtime_error {
public:
    SemanticError(std::string message, SourceLocation loc)
        : std::runtime_error(std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": Semantic error: " + message),
          location_(loc), raw_message_(std::move(message)) {}

    SourceLocation location() const noexcept { return location_; }
    const std::string& raw_message() const noexcept { return raw_message_; }

private:
    SourceLocation location_;
    std::string raw_message_;
};

struct Symbol {
    std::string name;
    TypeKind type;
    SourceLocation location;
};

class Scope {
public:
    explicit Scope(std::shared_ptr<Scope> parent = nullptr)
        : parent_(std::move(parent)) {}

    bool declare(const std::string& name, const Symbol& symbol) {
        if (symbols_.find(name) != symbols_.end()) {
            return false;
        }
        symbols_.emplace(name, symbol);
        return true;
    }

    const Symbol* resolve(const std::string& name) const {
        auto it = symbols_.find(name);
        if (it != symbols_.end()) {
            return &it->second;
        }
        if (parent_) {
            return parent_->resolve(name);
        }
        return nullptr;
    }

    std::shared_ptr<Scope> parent() const noexcept { return parent_; }

private:
    std::shared_ptr<Scope> parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

struct FunctionSignature {
    std::string name;
    TypeKind return_type;
    std::vector<TypeKind> param_types;
    SourceLocation location;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer() = default;

    void analyze(const ProgramAST& program);
    void analyze_function(const FunctionAST& function);
    void analyze_statement(const StmtNode& stmt);
    void analyze_block(const BlockStmtAST& block);
    TypeKind analyze_expression(const ExprNode& expr);

private:
    void enter_scope();
    void exit_scope();

    std::shared_ptr<Scope> current_scope_{nullptr};
    std::unordered_map<std::string, FunctionSignature> functions_;
    TypeKind current_function_return_type_{TypeKind::Void};
};

} // namespace phinale
