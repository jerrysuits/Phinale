#pragma once

#include <string>
#include <string_view>
#include <ostream>

namespace phinale {

enum class TokenKind {
    // Keywords
    KwInt,
    KwBool,
    KwIf,
    KwElse,
    KwWhile,
    KwReturn,

    // Literals & Identifiers
    IntLiteral,
    BoolLiteral,
    Identifier,

    // Operators
    Plus,          // +
    Minus,         // -
    Star,          // *
    Slash,         // /
    Equal,         // =
    EqualEqual,    // ==
    ExclaimEqual,  // !=
    Less,          // <
    LessEqual,     // <=
    Greater,       // >
    GreaterEqual,  // >=

    // Delimiters
    LParen,        // (
    RParen,        // )
    LBrace,        // {
    RBrace,        // }
    Semicolon,     // ;
    Comma,         // ,

    // Special
    Eof,
    Invalid
};

struct SourceLocation {
    std::size_t line{1};
    std::size_t column{1};

    bool operator==(const SourceLocation& other) const {
        return line == other.line && column == other.column;
    }

    bool operator!=(const SourceLocation& other) const {
        return !(*this == other);
    }
};

struct Token {
    TokenKind kind{TokenKind::Invalid};
    std::string lexeme;
    SourceLocation location;

    bool is(TokenKind expected_kind) const noexcept {
        return kind == expected_kind;
    }

    bool is_not(TokenKind expected_kind) const noexcept {
        return kind != expected_kind;
    }

    bool operator==(const Token& other) const {
        return kind == other.kind &&
               lexeme == other.lexeme &&
               location == other.location;
    }

    bool operator!=(const Token& other) const {
        return !(*this == other);
    }
};

std::string_view to_string(TokenKind kind) noexcept;
std::ostream& operator<<(std::ostream& os, TokenKind kind);
std::ostream& operator<<(std::ostream& os, const SourceLocation& loc);
std::ostream& operator<<(std::ostream& os, const Token& token);

} // namespace phinale
