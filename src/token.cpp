#include "phinale/token.hpp"

namespace phinale {

std::string_view to_string(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::KwInt:        return "int";
        case TokenKind::KwBool:       return "bool";
        case TokenKind::KwIf:         return "if";
        case TokenKind::KwElse:       return "else";
        case TokenKind::KwWhile:      return "while";
        case TokenKind::KwReturn:     return "return";

        case TokenKind::IntLiteral:   return "IntLiteral";
        case TokenKind::BoolLiteral:  return "BoolLiteral";
        case TokenKind::Identifier:   return "Identifier";

        case TokenKind::Plus:         return "+";
        case TokenKind::Minus:        return "-";
        case TokenKind::Star:         return "*";
        case TokenKind::Slash:        return "/";
        case TokenKind::Equal:        return "=";
        case TokenKind::EqualEqual:   return "==";
        case TokenKind::ExclaimEqual: return "!=";
        case TokenKind::Less:         return "<";
        case TokenKind::LessEqual:    return "<=";
        case TokenKind::Greater:      return ">";
        case TokenKind::GreaterEqual: return ">=";

        case TokenKind::LParen:       return "(";
        case TokenKind::RParen:       return ")";
        case TokenKind::LBrace:       return "{";
        case TokenKind::RBrace:       return "}";
        case TokenKind::Semicolon:    return ";";
        case TokenKind::Comma:        return ",";

        case TokenKind::Eof:          return "EOF";
        case TokenKind::Invalid:      return "INVALID";
    }
    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, TokenKind kind) {
    return os << to_string(kind);
}

std::ostream& operator<<(std::ostream& os, const SourceLocation& loc) {
    return os << loc.line << ':' << loc.column;
}

std::ostream& operator<<(std::ostream& os, const Token& token) {
    return os << "Token(" << to_string(token.kind) << ", \"" << token.lexeme << "\", " << token.location << ")";
}

} // namespace phinale
