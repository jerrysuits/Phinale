#include "phinale/parser.hpp"

#include <stdexcept>

namespace phinale {

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), current_(0) {}

Parser::Parser(Lexer& lexer)
    : tokens_(lexer.tokenize()), current_(0) {}

Parser::Parser(std::string source) {
    Lexer lexer(std::move(source));
    tokens_ = lexer.tokenize();
    current_ = 0;
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::is_at_end() const {
    return peek().kind == TokenKind::Eof;
}

bool Parser::check(TokenKind kind) const {
    if (is_at_end()) return kind == TokenKind::Eof;
    return peek().kind == kind;
}

const Token& Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }
    return previous();
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) {
        return advance();
    }
    throw ParseError(message, peek().location);
}

TypeKind Parser::parse_type() {
    if (match(TokenKind::KwInt)) {
        return TypeKind::Int;
    }
    if (match(TokenKind::KwBool)) {
        return TypeKind::Bool;
    }
    throw ParseError("Expected type name ('int' or 'bool')", peek().location);
}

std::unique_ptr<ProgramAST> Parser::parse_program() {
    SourceLocation loc = peek().location;
    std::vector<std::unique_ptr<FunctionAST>> functions;

    while (!is_at_end()) {
        functions.push_back(parse_function());
    }

    return std::make_unique<ProgramAST>(std::move(functions), loc);
}

std::unique_ptr<FunctionAST> Parser::parse_function() {
    SourceLocation loc = peek().location;
    TypeKind ret_type = parse_type();

    const Token& name_tok = consume(TokenKind::Identifier, "Expected function name");
    std::string name = name_tok.lexeme;

    consume(TokenKind::LParen, "Expected '(' after function name");

    std::vector<ParamAST> params;
    if (!check(TokenKind::RParen)) {
        do {
            SourceLocation param_loc = peek().location;
            TypeKind p_type = parse_type();
            const Token& p_name_tok = consume(TokenKind::Identifier, "Expected parameter name");
            params.push_back(ParamAST{p_type, p_name_tok.lexeme, param_loc});
        } while (match(TokenKind::Comma));
    }

    consume(TokenKind::RParen, "Expected ')' after parameters");

    std::unique_ptr<BlockStmtAST> body = parse_block();

    return std::make_unique<FunctionAST>(ret_type, std::move(name), std::move(params), std::move(body), loc);
}

std::unique_ptr<BlockStmtAST> Parser::parse_block() {
    SourceLocation loc = peek().location;
    consume(TokenKind::LBrace, "Expected '{' to start block");

    std::vector<std::unique_ptr<StmtNode>> statements;
    while (!check(TokenKind::RBrace) && !is_at_end()) {
        statements.push_back(parse_statement());
    }

    consume(TokenKind::RBrace, "Expected '}' after block");
    return std::make_unique<BlockStmtAST>(std::move(statements), loc);
}

std::unique_ptr<StmtNode> Parser::parse_statement() {
    if (check(TokenKind::KwInt) || check(TokenKind::KwBool)) {
        return parse_var_decl_statement();
    }
    if (check(TokenKind::KwIf)) {
        return parse_if_statement();
    }
    if (check(TokenKind::KwWhile)) {
        return parse_while_statement();
    }
    if (check(TokenKind::KwReturn)) {
        return parse_return_statement();
    }
    if (check(TokenKind::LBrace)) {
        return parse_block();
    }
    return parse_assignment_or_expression_statement();
}

std::unique_ptr<StmtNode> Parser::parse_var_decl_statement() {
    SourceLocation loc = peek().location;
    TypeKind type = parse_type();

    const Token& name_tok = consume(TokenKind::Identifier, "Expected variable name in declaration");
    std::string name = name_tok.lexeme;

    std::unique_ptr<ExprNode> init = nullptr;
    if (match(TokenKind::Equal)) {
        init = parse_expression();
    }

    consume(TokenKind::Semicolon, "Expected ';' after variable declaration");
    return std::make_unique<VarDeclStmtAST>(type, std::move(name), std::move(init), loc);
}

std::unique_ptr<StmtNode> Parser::parse_if_statement() {
    SourceLocation loc = advance().location; // Consume 'if'

    consume(TokenKind::LParen, "Expected '(' after 'if'");
    auto cond = parse_expression();
    consume(TokenKind::RParen, "Expected ')' after if condition");

    auto then_branch = parse_block();
    std::unique_ptr<StmtNode> else_branch = nullptr;

    if (match(TokenKind::KwElse)) {
        if (check(TokenKind::KwIf)) {
            else_branch = parse_if_statement();
        } else {
            else_branch = parse_block();
        }
    }

    return std::make_unique<IfStmtAST>(std::move(cond), std::move(then_branch), std::move(else_branch), loc);
}

std::unique_ptr<StmtNode> Parser::parse_while_statement() {
    SourceLocation loc = advance().location; // Consume 'while'

    consume(TokenKind::LParen, "Expected '(' after 'while'");
    auto cond = parse_expression();
    consume(TokenKind::RParen, "Expected ')' after while condition");

    auto body = parse_block();
    return std::make_unique<WhileStmtAST>(std::move(cond), std::move(body), loc);
}

std::unique_ptr<StmtNode> Parser::parse_return_statement() {
    SourceLocation loc = advance().location; // Consume 'return'

    std::unique_ptr<ExprNode> value = nullptr;
    if (!check(TokenKind::Semicolon)) {
        value = parse_expression();
    }

    consume(TokenKind::Semicolon, "Expected ';' after return statement");
    return std::make_unique<ReturnStmtAST>(std::move(value), loc);
}

std::unique_ptr<StmtNode> Parser::parse_assignment_or_expression_statement() {
    SourceLocation loc = peek().location;

    // Check if it's an assignment: Identifier followed by '='
    if (check(TokenKind::Identifier) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::Equal) {
        std::string var_name = advance().lexeme;
        advance(); // consume '='
        auto value = parse_expression();
        consume(TokenKind::Semicolon, "Expected ';' after assignment");
        return std::make_unique<AssignStmtAST>(std::move(var_name), std::move(value), loc);
    }

    auto expr = parse_expression();
    consume(TokenKind::Semicolon, "Expected ';' after expression statement");
    return std::make_unique<ExprStmtAST>(std::move(expr), loc);
}

std::unique_ptr<ExprNode> Parser::parse_expression() {
    return parse_assignment_expression();
}

std::unique_ptr<ExprNode> Parser::parse_assignment_expression() {
    return parse_equality();
}

std::unique_ptr<ExprNode> Parser::parse_equality() {
    auto expr = parse_relational();

    while (match(TokenKind::EqualEqual) || match(TokenKind::ExclaimEqual)) {
        Token op_token = previous();
        BinaryOp op = (op_token.kind == TokenKind::EqualEqual) ? BinaryOp::Equal : BinaryOp::NotEqual;
        auto rhs = parse_relational();
        expr = std::make_unique<BinaryExprAST>(op, std::move(expr), std::move(rhs), op_token.location);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::parse_relational() {
    auto expr = parse_additive();

    while (match(TokenKind::Less) || match(TokenKind::LessEqual) ||
           match(TokenKind::Greater) || match(TokenKind::GreaterEqual)) {
        Token op_token = previous();
        BinaryOp op;
        switch (op_token.kind) {
            case TokenKind::Less:         op = BinaryOp::Less; break;
            case TokenKind::LessEqual:    op = BinaryOp::LessEqual; break;
            case TokenKind::Greater:      op = BinaryOp::Greater; break;
            case TokenKind::GreaterEqual: op = BinaryOp::GreaterEqual; break;
            default: throw ParseError("Unexpected relational operator", op_token.location);
        }
        auto rhs = parse_additive();
        expr = std::make_unique<BinaryExprAST>(op, std::move(expr), std::move(rhs), op_token.location);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::parse_additive() {
    auto expr = parse_multiplicative();

    while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
        Token op_token = previous();
        BinaryOp op = (op_token.kind == TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        auto rhs = parse_multiplicative();
        expr = std::make_unique<BinaryExprAST>(op, std::move(expr), std::move(rhs), op_token.location);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::parse_multiplicative() {
    auto expr = parse_unary();

    while (match(TokenKind::Star) || match(TokenKind::Slash)) {
        Token op_token = previous();
        BinaryOp op = (op_token.kind == TokenKind::Star) ? BinaryOp::Mul : BinaryOp::Div;
        auto rhs = parse_unary();
        expr = std::make_unique<BinaryExprAST>(op, std::move(expr), std::move(rhs), op_token.location);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::parse_unary() {
    if (match(TokenKind::Minus)) {
        SourceLocation loc = previous().location;
        auto operand = parse_unary();
        return std::make_unique<UnaryExprAST>(UnaryOp::Negate, std::move(operand), loc);
    }
    return parse_primary();
}

std::unique_ptr<ExprNode> Parser::parse_primary() {
    SourceLocation loc = peek().location;

    if (match(TokenKind::IntLiteral)) {
        int64_t val = std::stoll(previous().lexeme);
        return std::make_unique<IntLiteralExprAST>(val, loc);
    }

    if (match(TokenKind::BoolLiteral)) {
        bool val = (previous().lexeme == "true");
        return std::make_unique<BoolLiteralExprAST>(val, loc);
    }

    if (match(TokenKind::Identifier)) {
        std::string name = previous().lexeme;

        if (match(TokenKind::LParen)) {
            std::vector<std::unique_ptr<ExprNode>> args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parse_expression());
                } while (match(TokenKind::Comma));
            }
            consume(TokenKind::RParen, "Expected ')' after function arguments");
            return std::make_unique<CallExprAST>(std::move(name), std::move(args), loc);
        }

        return std::make_unique<VariableExprAST>(std::move(name), loc);
    }

    if (match(TokenKind::LParen)) {
        auto expr = parse_expression();
        consume(TokenKind::RParen, "Expected ')' after expression");
        return expr;
    }

    throw ParseError("Expected expression, found '" + peek().lexeme + "'", loc);
}

} // namespace phinale
