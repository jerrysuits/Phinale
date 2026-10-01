#pragma once

#include "phinale/token.hpp"

#include <cstddef>
#include <istream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace phinale {

class Lexer {
public:
    explicit Lexer(std::string source);
    explicit Lexer(std::istream& stream);

    Token next_token();
    std::vector<Token> tokenize();

private:
    char peek() const noexcept;
    char peek_next() const noexcept;
    char advance() noexcept;
    bool match(char expected) noexcept;
    bool is_at_end() const noexcept;

    void skip_whitespace_and_comments() noexcept;

    Token make_token(TokenKind kind) const;
    Token make_token(TokenKind kind, std::string lexeme) const;
    Token error_token(std::string message) const;

    Token scan_identifier_or_keyword();
    Token scan_number();

    std::string source_;
    std::size_t start_{0};
    std::size_t current_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    std::size_t token_line_{1};
    std::size_t token_column_{1};
};

} // namespace phinale
