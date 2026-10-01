#pragma once

#include "phinale/token.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace phinale {

enum class TypeKind {
    Int,
    Bool,
    Void
};

inline std::string to_string(TypeKind type) {
    switch (type) {
        case TypeKind::Int: return "int";
        case TypeKind::Bool: return "bool";
        case TypeKind::Void: return "void";
    }
    return "unknown";
}

struct AstNode {
    SourceLocation location;

    explicit AstNode(SourceLocation loc) : location(loc) {}
    virtual ~AstNode() = default;
};

// Expressions

struct ExprNode : public AstNode {
    using AstNode::AstNode;
};

enum class BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

enum class UnaryOp {
    Negate,
    Not
};

struct BinaryExprAST : public ExprNode {
    BinaryOp op;
    std::unique_ptr<ExprNode> lhs;
    std::unique_ptr<ExprNode> rhs;

    BinaryExprAST(BinaryOp op, std::unique_ptr<ExprNode> lhs, std::unique_ptr<ExprNode> rhs, SourceLocation loc)
        : ExprNode(loc), op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

struct UnaryExprAST : public ExprNode {
    UnaryOp op;
    std::unique_ptr<ExprNode> operand;

    UnaryExprAST(UnaryOp op, std::unique_ptr<ExprNode> operand, SourceLocation loc)
        : ExprNode(loc), op(op), operand(std::move(operand)) {}
};

struct IntLiteralExprAST : public ExprNode {
    int64_t value;

    IntLiteralExprAST(int64_t value, SourceLocation loc)
        : ExprNode(loc), value(value) {}
};

struct BoolLiteralExprAST : public ExprNode {
    bool value;

    BoolLiteralExprAST(bool value, SourceLocation loc)
        : ExprNode(loc), value(value) {}
};

struct VariableExprAST : public ExprNode {
    std::string name;

    VariableExprAST(std::string name, SourceLocation loc)
        : ExprNode(loc), name(std::move(name)) {}
};

struct CallExprAST : public ExprNode {
    std::string callee;
    std::vector<std::unique_ptr<ExprNode>> args;

    CallExprAST(std::string callee, std::vector<std::unique_ptr<ExprNode>> args, SourceLocation loc)
        : ExprNode(loc), callee(std::move(callee)), args(std::move(args)) {}
};

// Statements

struct StmtNode : public AstNode {
    using AstNode::AstNode;
};

struct BlockStmtAST;

struct VarDeclStmtAST : public StmtNode {
    TypeKind type;
    std::string name;
    std::unique_ptr<ExprNode> initializer; // optional, can be nullptr

    VarDeclStmtAST(TypeKind type, std::string name, std::unique_ptr<ExprNode> init, SourceLocation loc)
        : StmtNode(loc), type(type), name(std::move(name)), initializer(std::move(init)) {}
};

struct AssignStmtAST : public StmtNode {
    std::string name;
    std::unique_ptr<ExprNode> value;

    AssignStmtAST(std::string name, std::unique_ptr<ExprNode> value, SourceLocation loc)
        : StmtNode(loc), name(std::move(name)), value(std::move(value)) {}
};

struct IfStmtAST : public StmtNode {
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<BlockStmtAST> then_branch;
    std::unique_ptr<StmtNode> else_branch; // can be BlockStmtAST or another IfStmtAST (else if) or nullptr

    IfStmtAST(std::unique_ptr<ExprNode> cond,
              std::unique_ptr<BlockStmtAST> then_b,
              std::unique_ptr<StmtNode> else_b,
              SourceLocation loc)
        : StmtNode(loc), condition(std::move(cond)), then_branch(std::move(then_b)), else_branch(std::move(else_b)) {}
};

struct WhileStmtAST : public StmtNode {
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<BlockStmtAST> body;

    WhileStmtAST(std::unique_ptr<ExprNode> cond, std::unique_ptr<BlockStmtAST> body, SourceLocation loc)
        : StmtNode(loc), condition(std::move(cond)), body(std::move(body)) {}
};

struct ReturnStmtAST : public StmtNode {
    std::unique_ptr<ExprNode> value; // can be nullptr for void return

    ReturnStmtAST(std::unique_ptr<ExprNode> val, SourceLocation loc)
        : StmtNode(loc), value(std::move(val)) {}
};

struct ExprStmtAST : public StmtNode {
    std::unique_ptr<ExprNode> expr;

    ExprStmtAST(std::unique_ptr<ExprNode> expr, SourceLocation loc)
        : StmtNode(loc), expr(std::move(expr)) {}
};

struct BlockStmtAST : public StmtNode {
    std::vector<std::unique_ptr<StmtNode>> statements;

    BlockStmtAST(std::vector<std::unique_ptr<StmtNode>> stmts, SourceLocation loc)
        : StmtNode(loc), statements(std::move(stmts)) {}
};

// Declarations & Program

struct ParamAST {
    TypeKind type;
    std::string name;
    SourceLocation location;
};

struct FunctionAST : public AstNode {
    TypeKind return_type;
    std::string name;
    std::vector<ParamAST> params;
    std::unique_ptr<BlockStmtAST> body;

    FunctionAST(TypeKind return_type,
                std::string name,
                std::vector<ParamAST> params,
                std::unique_ptr<BlockStmtAST> body,
                SourceLocation loc)
        : AstNode(loc),
          return_type(return_type),
          name(std::move(name)),
          params(std::move(params)),
          body(std::move(body)) {}
};

struct ProgramAST : public AstNode {
    std::vector<std::unique_ptr<FunctionAST>> functions;

    ProgramAST(std::vector<std::unique_ptr<FunctionAST>> funcs, SourceLocation loc)
        : AstNode(loc), functions(std::move(funcs)) {}
};

} // namespace phinale
