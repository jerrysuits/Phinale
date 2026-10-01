#include "phinale/lexer.hpp"

#include <cctype>
#include <sstream>
#include <unordered_map>

namespace phinale {

namespace {

bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

bool is_alphanumeric(char c) noexcept {
    return is_alpha(c) || is_digit(c);
}

const std::unordered_map<std::string_view, TokenKind>& keywords() {
    static const std::unordered_map<std::string_view, TokenKind> kw = {
        {"int",    TokenKind::KwInt},
        {"bool",   TokenKind::KwBool},
        {"if",     TokenKind::KwIf},
        {"else",   TokenKind::KwElse},
        {"while",  TokenKind::KwWhile},
        {"return", TokenKind::KwReturn},
        {"true",   TokenKind::BoolLiteral},
        {"false",  TokenKind::BoolLiteral},
    };
    return kw;
}

std::string read_all_from_stream(std::istream& stream) {
    std::ostringstream ss;
    ss << stream.rdbuf();
    return ss.str();
}

} // namespace

Lexer::Lexer(std::string source)
    : source_(std::move(source)),
      start_(0),
      current_(0),
      line_(1),
      column_(1),
      token_line_(1),
      token_column_(1) {}

Lexer::Lexer(std::istream& stream)
    : Lexer(read_all_from_stream(stream)) {}

char Lexer::peek() const noexcept {
    if (is_at_end()) return '\0';
    return source_[current_];
}

char Lexer::peek_next() const noexcept {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

char Lexer::advance() noexcept {
    char c = source_[current_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::match(char expected) noexcept {
    if (is_at_end()) return false;
    if (source_[current_] != expected) return false;
    advance();
    return true;
}

bool Lexer::is_at_end() const noexcept {
    return current_ >= source_.size();
}

void Lexer::skip_whitespace_and_comments() noexcept {
    while (!is_at_end()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
            case '\n':
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    // Line comment: consume until '\n' or EOF
                    advance(); // consume '/'
                    advance(); // consume '/'
                    while (!is_at_end() && peek() != '\n') {
                        advance();
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token Lexer::make_token(TokenKind kind) const {
    std::string lexeme = source_.substr(start_, current_ - start_);
    return Token{kind, std::move(lexeme), SourceLocation{token_line_, token_column_}};
}

Token Lexer::make_token(TokenKind kind, std::string lexeme) const {
    return Token{kind, std::move(lexeme), SourceLocation{token_line_, token_column_}};
}

Token Lexer::error_token(std::string message) const {
    return Token{TokenKind::Invalid, std::move(message), SourceLocation{token_line_, token_column_}};
}

Token Lexer::scan_identifier_or_keyword() {
    while (!is_at_end() && is_alphanumeric(peek())) {
        advance();
    }

    std::string_view text(source_.data() + start_, current_ - start_);
    const auto& kw = keywords();
    auto it = kw.find(text);
    if (it != kw.end()) {
        return make_token(it->second);
    }
    return make_token(TokenKind::Identifier);
}

Token Lexer::scan_number() {
    while (!is_at_end() && is_digit(peek())) {
        advance();
    }
    return make_token(TokenKind::IntLiteral);
}

Token Lexer::next_token() {
    skip_whitespace_and_comments();

    start_ = current_;
    token_line_ = line_;
    token_column_ = column_;

    if (is_at_end()) {
        return make_token(TokenKind::Eof, "");
    }

    char c = advance();

    if (is_digit(c)) {
        return scan_number();
    }

    if (is_alpha(c)) {
        return scan_identifier_or_keyword();
    }

    switch (c) {
        // Delimiters
        case '(': return make_token(TokenKind::LParen);
        case ')': return make_token(TokenKind::RParen);
        case '{': return make_token(TokenKind::LBrace);
        case '}': return make_token(TokenKind::RBrace);
        case ';': return make_token(TokenKind::Semicolon);
        case ',': return make_token(TokenKind::Comma);

        // Arithmetic Operators
        case '+': return make_token(TokenKind::Plus);
        case '-': return make_token(TokenKind::Minus);
        case '*': return make_token(TokenKind::Star);
        case '/': return make_token(TokenKind::Slash);

        // Equality / Relational / Assignment Operators
        case '=':
            return make_token(match('=') ? TokenKind::EqualEqual : TokenKind::Equal);
        case '!':
            if (match('=')) {
                return make_token(TokenKind::ExclaimEqual);
            }
            return error_token(std::string(1, c));
        case '<':
            return make_token(match('=') ? TokenKind::LessEqual : TokenKind::Less);
        case '>':
            return make_token(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater);

        default:
            return error_token(std::string(1, c));
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = next_token();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::Eof) {
            break;
        }
    }
    return tokens;
}

} // namespace phinale
