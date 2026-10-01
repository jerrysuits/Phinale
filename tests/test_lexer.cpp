#include "phinale/lexer.hpp"
#include "phinale/token.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <vector>

void test_keywords_vs_identifiers() {
    std::string source = "int foo bool boolean if ifelse else while while1 return returning true trueVal false false_val";
    phinale::Lexer lexer(source);
    auto tokens = lexer.tokenize();

    struct Expected {
        phinale::TokenKind kind;
        std::string lexeme;
    };

    std::vector<Expected> expected = {
        {phinale::TokenKind::KwInt, "int"},
        {phinale::TokenKind::Identifier, "foo"},
        {phinale::TokenKind::KwBool, "bool"},
        {phinale::TokenKind::Identifier, "boolean"},
        {phinale::TokenKind::KwIf, "if"},
        {phinale::TokenKind::Identifier, "ifelse"},
        {phinale::TokenKind::KwElse, "else"},
        {phinale::TokenKind::KwWhile, "while"},
        {phinale::TokenKind::Identifier, "while1"},
        {phinale::TokenKind::KwReturn, "return"},
        {phinale::TokenKind::Identifier, "returning"},
        {phinale::TokenKind::BoolLiteral, "true"},
        {phinale::TokenKind::Identifier, "trueVal"},
        {phinale::TokenKind::BoolLiteral, "false"},
        {phinale::TokenKind::Identifier, "false_val"},
        {phinale::TokenKind::Eof, ""},
    };

    assert(tokens.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        assert(tokens[i].kind == expected[i].kind);
        assert(tokens[i].lexeme == expected[i].lexeme);
    }
}

void test_multi_char_operators_and_delimiters() {
    std::string source = "+ - * / = == != < <= > >= ( ) { } ; ,";
    phinale::Lexer lexer(source);
    auto tokens = lexer.tokenize();

    std::vector<phinale::TokenKind> expected = {
        phinale::TokenKind::Plus,
        phinale::TokenKind::Minus,
        phinale::TokenKind::Star,
        phinale::TokenKind::Slash,
        phinale::TokenKind::Equal,
        phinale::TokenKind::EqualEqual,
        phinale::TokenKind::ExclaimEqual,
        phinale::TokenKind::Less,
        phinale::TokenKind::LessEqual,
        phinale::TokenKind::Greater,
        phinale::TokenKind::GreaterEqual,
        phinale::TokenKind::LParen,
        phinale::TokenKind::RParen,
        phinale::TokenKind::LBrace,
        phinale::TokenKind::RBrace,
        phinale::TokenKind::Semicolon,
        phinale::TokenKind::Comma,
        phinale::TokenKind::Eof,
    };

    assert(tokens.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        assert(tokens[i].kind == expected[i]);
    }
}

void test_source_position_tracking_and_comments() {
    std::string source =
        "// this is a comment\n"
        "int a = 42;\n"
        "   // another comment\n"
        "return a == 42;\n";

    phinale::Lexer lexer(source);
    auto tokens = lexer.tokenize();

    // Line 2: "int a = 42;"
    // 'int' at 2:1
    assert(tokens[0].kind == phinale::TokenKind::KwInt);
    assert(tokens[0].location.line == 2 && tokens[0].location.column == 1);

    // 'a' at 2:5
    assert(tokens[1].kind == phinale::TokenKind::Identifier);
    assert(tokens[1].lexeme == "a");
    assert(tokens[1].location.line == 2 && tokens[1].location.column == 5);

    // '=' at 2:7
    assert(tokens[2].kind == phinale::TokenKind::Equal);
    assert(tokens[2].location.line == 2 && tokens[2].location.column == 7);

    // '42' at 2:9
    assert(tokens[3].kind == phinale::TokenKind::IntLiteral);
    assert(tokens[3].lexeme == "42");
    assert(tokens[3].location.line == 2 && tokens[3].location.column == 9);

    // ';' at 2:11
    assert(tokens[4].kind == phinale::TokenKind::Semicolon);
    assert(tokens[4].location.line == 2 && tokens[4].location.column == 11);

    // Line 4: "return a == 42;"
    // 'return' at 4:1
    assert(tokens[5].kind == phinale::TokenKind::KwReturn);
    assert(tokens[5].location.line == 4 && tokens[5].location.column == 1);

    // 'a' at 4:8
    assert(tokens[6].kind == phinale::TokenKind::Identifier);
    assert(tokens[6].lexeme == "a");
    assert(tokens[6].location.line == 4 && tokens[6].location.column == 8);

    // '==' at 4:10
    assert(tokens[7].kind == phinale::TokenKind::EqualEqual);
    assert(tokens[7].lexeme == "==");
    assert(tokens[7].location.line == 4 && tokens[7].location.column == 10);

    // '42' at 4:13
    assert(tokens[8].kind == phinale::TokenKind::IntLiteral);
    assert(tokens[8].location.line == 4 && tokens[8].location.column == 13);

    // ';' at 4:15
    assert(tokens[9].kind == phinale::TokenKind::Semicolon);
    assert(tokens[9].location.line == 4 && tokens[9].location.column == 15);

    // EOF at 5:1
    assert(tokens[10].kind == phinale::TokenKind::Eof);
    assert(tokens[10].location.line == 5 && tokens[10].location.column == 1);
}

void test_invalid_characters() {
    std::string source = "@ $ # !";
    phinale::Lexer lexer(source);
    auto tokens = lexer.tokenize();

    assert(tokens.size() == 5);
    assert(tokens[0].kind == phinale::TokenKind::Invalid && tokens[0].lexeme == "@");
    assert(tokens[1].kind == phinale::TokenKind::Invalid && tokens[1].lexeme == "$");
    assert(tokens[2].kind == phinale::TokenKind::Invalid && tokens[2].lexeme == "#");
    // Single '!' without '=' is invalid
    assert(tokens[3].kind == phinale::TokenKind::Invalid && tokens[3].lexeme == "!");
    assert(tokens[4].kind == phinale::TokenKind::Eof);
}

void test_stream_input() {
    std::istringstream stream("while (x <= 10) { x = x + 1; }");
    phinale::Lexer lexer(stream);
    auto tokens = lexer.tokenize();

    assert(!tokens.empty());
    assert(tokens.front().kind == phinale::TokenKind::KwWhile);
    assert(tokens.back().kind == phinale::TokenKind::Eof);
}

int main() {
    test_keywords_vs_identifiers();
    test_multi_char_operators_and_delimiters();
    test_source_position_tracking_and_comments();
    test_invalid_characters();
    test_stream_input();

    std::cout << "All lexer tests passed successfully!\n";
    return 0;
}
