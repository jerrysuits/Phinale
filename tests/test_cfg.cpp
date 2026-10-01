#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/parser.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

void test_straight_line() {
    std::string src =
        "int calc() {\n"
        "    int a = 1;\n"
        "    int b = 2;\n"
        "    int c = a + b;\n"
        "    return c;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();

    phinale::IRBuilder ir_builder;
    auto tac_prog = ir_builder.build_program(*program);
    assert(tac_prog.functions.size() == 1);

    phinale::CFGBuilder cfg_builder;
    auto cfg = cfg_builder.build(tac_prog.functions[0]);

    // Straight-line code: single basic block, 0 branches, entry == exit
    assert(cfg.size() == 1);
    assert(cfg.entry() != nullptr);
    assert(cfg.exits().size() == 1);
    assert(cfg.entry() == cfg.exits()[0]);
    assert(cfg.entry()->successors().empty());
    assert(cfg.entry()->predecessors().empty());
    assert(cfg.entry()->is_terminated());
}

void test_if_else_cfg() {
    std::string src =
        "int max(int a, int b) {\n"
        "    int res = 0;\n"
        "    if (a > b) {\n"
        "        res = a;\n"
        "    } else {\n"
        "        res = b;\n"
        "    }\n"
        "    return res;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();

    phinale::IRBuilder ir_builder;
    auto tac_prog = ir_builder.build_program(*program);
    assert(tac_prog.functions.size() == 1);

    phinale::CFGBuilder cfg_builder;
    auto cfg = cfg_builder.build(tac_prog.functions[0]);

    // Expected structure:
    // Entry block: condition evaluation + JUMP_IF to then/else
    // Then block: executes then branch + JUMP to join
    // Else block: executes else branch (fall-through to join)
    // Join block: return res
    assert(cfg.size() >= 4);

    auto* entry = cfg.entry();
    assert(entry != nullptr);
    // Entry block ends with JUMP_IF -> 2 successors
    assert(entry->successors().size() == 2);

    auto* then_block = entry->successors()[0];
    auto* else_block = entry->successors()[1];
    assert(then_block != else_block);

    // Both then and else should have entry as a predecessor
    bool entry_in_then_pred = false;
    for (auto* p : then_block->predecessors()) {
        if (p == entry) entry_in_then_pred = true;
    }
    assert(entry_in_then_pred);

    bool entry_in_else_pred = false;
    for (auto* p : else_block->predecessors()) {
        if (p == entry) entry_in_else_pred = true;
    }
    assert(entry_in_else_pred);

    // Both should eventually lead to the join/exit block
    assert(!cfg.exits().empty());
    auto* exit_block = cfg.exits()[0];
    assert(exit_block->is_terminated());
    assert(exit_block->instructions().back().opcode == phinale::Opcode::RETURN);
}

void test_while_loop_cfg() {
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

    phinale::IRBuilder ir_builder;
    auto tac_prog = ir_builder.build_program(*program);
    assert(tac_prog.functions.size() == 1);

    phinale::CFGBuilder cfg_builder;
    auto cfg = cfg_builder.build(tac_prog.functions[0]);

    // Find the loop header block (the one with JUMP_IF)
    phinale::BasicBlock* header = nullptr;
    for (const auto& bb : cfg.blocks()) {
        if (!bb->instructions().empty() && bb->instructions().back().opcode == phinale::Opcode::JUMP_IF) {
            header = bb.get();
            break;
        }
    }
    assert(header != nullptr);
    assert(header->successors().size() == 2);

    // One successor is loop body, one is exit
    auto* body = header->successors()[0];
    auto* exit_succ = header->successors()[1];
    assert(exit_succ != nullptr);

    // Body block should have a back-edge pointing back to header
    bool has_back_edge = false;
    for (auto* succ : body->successors()) {
        if (succ == header) {
            has_back_edge = true;
        }
    }
    assert(has_back_edge);

    // Header has at least 2 predecessors: entry/pre-loop and loop body
    assert(header->predecessors().size() >= 2);

    // Check Reverse Post-Order traversal
    auto rpo = cfg.reverse_post_order();
    assert(!rpo.empty());
    assert(rpo[0] == cfg.entry());
}

void test_dot_generation() {
    std::string src =
        "int main() {\n"
        "    int x = 42;\n"
        "    if (x > 0) {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";

    phinale::Parser parser(src);
    auto program = parser.parse_program();

    phinale::IRBuilder ir_builder;
    auto tac_prog = ir_builder.build_program(*program);
    assert(tac_prog.functions.size() == 1);

    phinale::CFGBuilder cfg_builder;
    auto cfg = cfg_builder.build(tac_prog.functions[0]);

    std::ostringstream ss;
    cfg.dump_dot(ss);
    std::string dot = ss.str();

    assert(!dot.empty());
    assert(dot.find("digraph CFG") != std::string::npos);
    assert(dot.find("bb0") != std::string::npos);
    assert(dot.find("->") != std::string::npos);
}

int main() {
    test_straight_line();
    test_if_else_cfg();
    test_while_loop_cfg();
    test_dot_generation();

    std::cout << "All CFG tests passed successfully!\n";
    return 0;
}
