#pragma once

#include "phinale/ast.hpp"
#include "phinale/lexer.hpp"
#include "phinale/token.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace phinale {

class ParseError : public std::runtime_error {
public:
    ParseError(std::string message, SourceLocation loc)
        : std::runtime_error(std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": Parse error: " + message),
          location_(loc), raw_message_(std::move(message)) {}

    SourceLocation location() const noexcept { return location_; }
    const std::string& raw_message() const noexcept { return raw_message_; }

private:
    SourceLocation location_;
    std::string raw_message_;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    explicit Parser(Lexer& lexer);
    explicit Parser(std::string source);

    std::unique_ptr<ProgramAST> parse_program();
    std::unique_ptr<FunctionAST> parse_function();
    std::unique_ptr<StmtNode> parse_statement();
    std::unique_ptr<BlockStmtAST> parse_block();
    std::unique_ptr<ExprNode> parse_expression();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool is_at_end() const;
    bool check(TokenKind kind) const;
    const Token& advance();
    bool match(TokenKind kind);
    const Token& consume(TokenKind kind, const std::string& message);

    TypeKind parse_type();

    std::unique_ptr<StmtNode> parse_var_decl_statement();
    std::unique_ptr<StmtNode> parse_if_statement();
    std::unique_ptr<StmtNode> parse_while_statement();
    std::unique_ptr<StmtNode> parse_return_statement();
    std::unique_ptr<StmtNode> parse_assignment_or_expression_statement();

    std::unique_ptr<ExprNode> parse_assignment_expression();
    std::unique_ptr<ExprNode> parse_equality();
    std::unique_ptr<ExprNode> parse_relational();
    std::unique_ptr<ExprNode> parse_additive();
    std::unique_ptr<ExprNode> parse_multiplicative();
    std::unique_ptr<ExprNode> parse_unary();
    std::unique_ptr<ExprNode> parse_primary();

    std::vector<Token> tokens_;
    std::size_t current_{0};
};

} // namespace phinale
