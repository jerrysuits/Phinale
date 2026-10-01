#include "phinale/lexer.hpp"
#include "phinale/parser.hpp"
#include "phinale/sema.hpp"

#include <cassert>
#include <iostream>
#include <string>

void test_operator_precedence() {
    // a + b * c == d < e
    // Expected AST tree:
    // == (Equality)
    //   lhs: + (Additive)
    //          lhs: a
    //          rhs: * (Multiplicative: b * c)
    //   rhs: < (Relational: d < e)
    std::string src =
        "int test() {\n"
        "    int a = 1;\n"
        "    int b = 2;\n"
        "    int c = 3;\n"
        "    int d = 4;\n"
        "    int e = 5;\n"
        "    bool res = a + b * c == d < e;\n"
        "    return 0;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();
    assert(program->functions.size() == 1);
    const auto& func = program->functions[0];
    assert(func->body->statements.size() == 7);

    // Look at statement 5: bool res = a + b * c == d < e;
    auto* var_decl = dynamic_cast<phinale::VarDeclStmtAST*>(func->body->statements[5].get());
    assert(var_decl != nullptr);
    assert(var_decl->name == "res");

    // Top expr should be '=='
    auto* eq_expr = dynamic_cast<phinale::BinaryExprAST*>(var_decl->initializer.get());
    assert(eq_expr != nullptr);
    assert(eq_expr->op == phinale::BinaryOp::Equal);

    // Left child of '==' should be '+'
    auto* add_expr = dynamic_cast<phinale::BinaryExprAST*>(eq_expr->lhs.get());
    assert(add_expr != nullptr);
    assert(add_expr->op == phinale::BinaryOp::Add);

    // Right child of '+' should be '*'
    auto* mul_expr = dynamic_cast<phinale::BinaryExprAST*>(add_expr->rhs.get());
    assert(mul_expr != nullptr);
    assert(mul_expr->op == phinale::BinaryOp::Mul);

    // Right child of '==' should be '<'
    auto* lt_expr = dynamic_cast<phinale::BinaryExprAST*>(eq_expr->rhs.get());
    assert(lt_expr != nullptr);
    assert(lt_expr->op == phinale::BinaryOp::Less);
}

void test_if_else_and_nested_if() {
    std::string src =
        "int foo(int x) {\n"
        "    if (x > 0) {\n"
        "        if (x == 1) {\n"
        "            return 10;\n"
        "        } else {\n"
        "            return 20;\n"
        "        }\n"
        "    } else if (x < 0) {\n"
        "        return -1;\n"
        "    } else {\n"
        "        return 0;\n"
        "    }\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();
    assert(program->functions.size() == 1);

    const auto& func = program->functions[0];
    assert(func->body->statements.size() == 1);

    auto* if_stmt = dynamic_cast<phinale::IfStmtAST*>(func->body->statements[0].get());
    assert(if_stmt != nullptr);
    assert(if_stmt->then_branch->statements.size() == 1);

    // Inner if
    auto* nested_if = dynamic_cast<phinale::IfStmtAST*>(if_stmt->then_branch->statements[0].get());
    assert(nested_if != nullptr);
    assert(nested_if->else_branch != nullptr);

    // Outer else-if
    assert(if_stmt->else_branch != nullptr);
    auto* else_if = dynamic_cast<phinale::IfStmtAST*>(if_stmt->else_branch.get());
    assert(else_if != nullptr);
    assert(else_if->else_branch != nullptr);

    phinale::SemanticAnalyzer sema;
    sema.analyze(*program);
}

void test_while_loop_and_assignment() {
    std::string src =
        "int sum(int n) {\n"
        "    int s = 0;\n"
        "    int i = 1;\n"
        "    while (i <= n) {\n"
        "        s = s + i;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return s;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();
    assert(program->functions.size() == 1);

    const auto& func = program->functions[0];
    assert(func->body->statements.size() == 4);

    auto* while_stmt = dynamic_cast<phinale::WhileStmtAST*>(func->body->statements[2].get());
    assert(while_stmt != nullptr);
    assert(while_stmt->body->statements.size() == 2);

    auto* assign1 = dynamic_cast<phinale::AssignStmtAST*>(while_stmt->body->statements[0].get());
    assert(assign1 != nullptr);
    assert(assign1->name == "s");

    phinale::SemanticAnalyzer sema;
    sema.analyze(*program);
}

void test_semantic_errors() {
    // Case 1: Type mismatch in variable initialization
    {
        std::string src =
            "int main() {\n"
            "    int a = true;\n"
            "    return 0;\n"
            "}\n";
        phinale::Parser parser(src);
        auto program = parser.parse_program();
        phinale::SemanticAnalyzer sema;
        bool caught = false;
        try {
            sema.analyze(*program);
        } catch (const phinale::SemanticError&) {
            caught = true;
        }
        assert(caught);
    }

    // Case 2: Undeclared variable reference
    {
        std::string src =
            "int main() {\n"
            "    int a = b + 1;\n"
            "    return 0;\n"
            "}\n";
        phinale::Parser parser(src);
        auto program = parser.parse_program();
        phinale::SemanticAnalyzer sema;
        bool caught = false;
        try {
            sema.analyze(*program);
        } catch (const phinale::SemanticError&) {
            caught = true;
        }
        assert(caught);
    }

    // Case 3: Duplicate declaration in same scope
    {
        std::string src =
            "int main() {\n"
            "    int a = 1;\n"
            "    bool a = false;\n"
            "    return 0;\n"
            "}\n";
        phinale::Parser parser(src);
        auto program = parser.parse_program();
        phinale::SemanticAnalyzer sema;
        bool caught = false;
        try {
            sema.analyze(*program);
        } catch (const phinale::SemanticError&) {
            caught = true;
        }
        assert(caught);
    }

    // Case 4: Non-boolean condition in if statement
    {
        std::string src =
            "int main() {\n"
            "    if (123) {\n"
            "        return 1;\n"
            "    }\n"
            "    return 0;\n"
            "}\n";
        phinale::Parser parser(src);
        auto program = parser.parse_program();
        phinale::SemanticAnalyzer sema;
        bool caught = false;
        try {
            sema.analyze(*program);
        } catch (const phinale::SemanticError&) {
            caught = true;
        }
        assert(caught);
    }

    // Case 5: Function call arity / type mismatch
    {
        std::string src =
            "int add(int x, int y) {\n"
            "    return x + y;\n"
            "}\n"
            "int main() {\n"
            "    return add(1);\n"
            "}\n";
        phinale::Parser parser(src);
        auto program = parser.parse_program();
        phinale::SemanticAnalyzer sema;
        bool caught = false;
        try {
            sema.analyze(*program);
        } catch (const phinale::SemanticError&) {
            caught = true;
        }
        assert(caught);
    }
}

int main() {
    test_operator_precedence();
    test_if_else_and_nested_if();
    test_while_loop_and_assignment();
    test_semantic_errors();

    std::cout << "All parser and sema tests passed successfully!\n";
    return 0;
}
