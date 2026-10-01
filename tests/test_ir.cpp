#include "phinale/ir_builder.hpp"
#include "phinale/parser.hpp"
#include "phinale/tac.h"

#include <cassert>
#include <iostream>
#include <string>

void test_lower_arithmetic() {
    std::string src =
        "int calc() {\n"
        "    int a = 10;\n"
        "    int b = 20;\n"
        "    int c = a + b * 2;\n"
        "    return c;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();

    phinale::IRBuilder builder;
    phinale::TACProgram tac = builder.build_program(*program);

    assert(tac.functions.size() == 1);
    const auto& func = tac.functions[0];
    assert(func.name == "calc");

    // Check that we have instructions generated:
    // v0 = 10
    // v1 = 20
    // v3 = v1 * 2
    // v4 = v0 + v3
    // v2 = v4
    // return v2
    bool has_mul = false;
    bool has_add = false;
    bool has_return = false;

    for (const auto& inst : func.instructions) {
        if (inst.opcode == phinale::Opcode::MUL) {
            has_mul = true;
            assert(inst.src1.has_value() && inst.src2.has_value());
            assert(inst.src2->is_int() && inst.src2->as_int() == 2);
        } else if (inst.opcode == phinale::Opcode::ADD) {
            has_add = true;
        } else if (inst.opcode == phinale::Opcode::RETURN) {
            has_return = true;
            assert(inst.src1.has_value());
        }
    }

    assert(has_mul);
    assert(has_add);
    assert(has_return);
}

void test_lower_if_else() {
    std::string src =
        "int abs_val(int x) {\n"
        "    if (x < 0) {\n"
        "        return -x;\n"
        "    } else {\n"
        "        return x;\n"
        "    }\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();

    phinale::IRBuilder builder;
    phinale::TACProgram tac = builder.build_program(*program);

    assert(tac.functions.size() == 1);
    const auto& func = tac.functions[0];

    bool has_cmp_lt = false;
    bool has_jump_if = false;
    bool has_jump = false;
    int label_count = 0;

    for (const auto& inst : func.instructions) {
        if (inst.opcode == phinale::Opcode::CMP_LT) {
            has_cmp_lt = true;
        } else if (inst.opcode == phinale::Opcode::JUMP_IF) {
            has_jump_if = true;
            assert(inst.label_target.has_value());
            assert(inst.false_label_target.has_value());
        } else if (inst.opcode == phinale::Opcode::JUMP) {
            has_jump = true;
            assert(inst.label_target.has_value());
        } else if (inst.opcode == phinale::Opcode::LABEL) {
            label_count++;
        }
    }

    assert(has_cmp_lt);
    assert(has_jump_if);
    assert(has_jump);
    // Then label, Else label, Endif label
    assert(label_count >= 3);
}

void test_lower_while_loop() {
    std::string src =
        "int loop_test(int n) {\n"
        "    int i = 0;\n"
        "    while (i < n) {\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return i;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();

    phinale::IRBuilder builder;
    phinale::TACProgram tac = builder.build_program(*program);

    assert(tac.functions.size() == 1);
    const auto& func = tac.functions[0];

    bool has_header_label = false;
    bool has_jump_if = false;
    bool has_back_edge = false;
    std::string header_label_name;

    for (const auto& inst : func.instructions) {
        if (inst.opcode == phinale::Opcode::LABEL && !has_header_label) {
            has_header_label = true;
            header_label_name = *inst.label_target;
        } else if (inst.opcode == phinale::Opcode::JUMP_IF) {
            has_jump_if = true;
        } else if (inst.opcode == phinale::Opcode::JUMP) {
            if (inst.label_target == header_label_name) {
                has_back_edge = true;
            }
        }
    }

    assert(has_header_label);
    assert(has_jump_if);
    assert(has_back_edge);
}

void test_tac_pretty_print() {
    std::string src =
        "int main() {\n"
        "    int a = 10;\n"
        "    int b = 20;\n"
        "    if (a < b) {\n"
        "        return a;\n"
        "    }\n"
        "    return b;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();

    phinale::IRBuilder builder;
    phinale::TACProgram tac = builder.build_program(*program);
    std::string printed = phinale::to_string(tac);

    assert(!printed.empty());
    assert(printed.find("fn main():") != std::string::npos);
    assert(printed.find("return") != std::string::npos);
}

int main() {
    test_lower_arithmetic();
    test_lower_if_else();
    test_lower_while_loop();
    test_tac_pretty_print();

    std::cout << "All IR lowering tests passed successfully!\n";
    return 0;
}
