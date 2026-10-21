#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/dominance.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/parser.hpp"

#include <cassert>
#include <iostream>
#include <string>

static void add_edge(phinale::BasicBlock* from, phinale::BasicBlock* to) {
    if (!from || !to) return;
    from->add_successor(to);
    to->add_predecessor(from);
}

void test_straight_line() {
    // 1. Programmatic multi-block straight-line CFG: B0 -> B1 -> B2 -> B3
    {
        phinale::ControlFlowGraph cfg;
        auto* b0 = cfg.create_block("b0");
        auto* b1 = cfg.create_block("b1");
        auto* b2 = cfg.create_block("b2");
        auto* b3 = cfg.create_block("b3");

        cfg.set_entry(b0);
        cfg.add_exit(b3);

        add_edge(b0, b1);
        add_edge(b1, b2);
        add_edge(b2, b3);

        phinale::DominanceAnalysis da(cfg);

        // Verify sequential idom chains
        assert(da.get_idom(b0->id()) == b0->id());
        assert(da.get_idom(b1->id()) == b0->id());
        assert(da.get_idom(b2->id()) == b1->id());
        assert(da.get_idom(b3->id()) == b2->id());

        // Verify dominance queries
        assert(da.dominates(b0->id(), b0->id()));
        assert(da.dominates(b0->id(), b1->id()));
        assert(da.dominates(b0->id(), b2->id()));
        assert(da.dominates(b0->id(), b3->id()));
        assert(da.dominates(b1->id(), b2->id()));
        assert(da.dominates(b1->id(), b3->id()));
        assert(da.dominates(b2->id(), b3->id()));

        assert(!da.dominates(b1->id(), b0->id()));
        assert(!da.dominates(b2->id(), b1->id()));
        assert(!da.dominates(b3->id(), b2->id()));

        assert(!da.strictly_dominates(b0->id(), b0->id()));
        assert(da.strictly_dominates(b0->id(), b1->id()));
        assert(da.strictly_dominates(b1->id(), b2->id()));
        assert(da.strictly_dominates(b2->id(), b3->id()));

        // Verify dominator tree children
        assert(da.dom_tree_children.at(b0->id()).size() == 1);
        assert(da.dom_tree_children.at(b0->id())[0] == b1->id());
        assert(da.dom_tree_children.at(b1->id()).size() == 1);
        assert(da.dom_tree_children.at(b1->id())[0] == b2->id());
        assert(da.dom_tree_children.at(b2->id()).size() == 1);
        assert(da.dom_tree_children.at(b2->id())[0] == b3->id());
        assert(da.dom_tree_children.at(b3->id()).empty());

        // Verify empty dominance frontiers
        assert(da.get_frontier(b0->id()).empty());
        assert(da.get_frontier(b1->id()).empty());
        assert(da.get_frontier(b2->id()).empty());
        assert(da.get_frontier(b3->id()).empty());
    }

    // 2. End-to-end parsed straight-line code
    {
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

        phinale::DominanceAnalysis da(cfg);
        assert(cfg.entry() != nullptr);
        assert(da.get_idom(cfg.entry()->id()) == cfg.entry()->id());
        assert(da.get_frontier(cfg.entry()->id()).empty());
    }
}

void test_diamond_if_else() {
    // 1. Programmatic diamond CFG:
    //      B0 (branch block)
    //     /  \ (split)
    //   B1    B2 (branch exits)
    //     \  /
    //      B3 (join block)
    {
        phinale::ControlFlowGraph cfg;
        auto* b0 = cfg.create_block("branch");
        auto* b1 = cfg.create_block("then");
        auto* b2 = cfg.create_block("else");
        auto* b3 = cfg.create_block("join");

        cfg.set_entry(b0);
        cfg.add_exit(b3);

        add_edge(b0, b1);
        add_edge(b0, b2);
        add_edge(b1, b3);
        add_edge(b2, b3);

        phinale::DominanceAnalysis da(cfg);

        // Immediate dominators: B0 dominates all blocks
        assert(da.get_idom(b0->id()) == b0->id());
        assert(da.get_idom(b1->id()) == b0->id());
        assert(da.get_idom(b2->id()) == b0->id());
        assert(da.get_idom(b3->id()) == b0->id());

        // Verify branch block dominates join block
        assert(da.dominates(b0->id(), b3->id()));
        assert(da.strictly_dominates(b0->id(), b3->id()));

        // Neither branch arm alone dominates join block
        assert(!da.dominates(b1->id(), b3->id()));
        assert(!da.dominates(b2->id(), b3->id()));

        // Verify DF of branch exits includes the join point
        assert(da.get_frontier(b1->id()).count(b3->id()) == 1);
        assert(da.get_frontier(b2->id()).count(b3->id()) == 1);

        // DF of branch block and join block should be empty
        assert(da.get_frontier(b0->id()).empty());
        assert(da.get_frontier(b3->id()).empty());
    }

    // 2. End-to-end parsed if-else
    {
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

        auto* entry = cfg.entry();
        assert(entry != nullptr);
        assert(entry->successors().size() == 2);

        auto* then_block = entry->successors()[0];
        auto* else_block = entry->successors()[1];
        assert(!cfg.exits().empty());
        auto* join_block = cfg.exits()[0];

        phinale::DominanceAnalysis da(cfg);

        // Verify branch block dominates join block
        assert(da.dominates(entry->id(), join_block->id()));
        assert(da.strictly_dominates(entry->id(), join_block->id()));

        // Verify DF of branch exits includes the join point
        assert(da.get_frontier(then_block->id()).count(join_block->id()) == 1);
        assert(da.get_frontier(else_block->id()).count(join_block->id()) == 1);
    }
}

void test_loop_while() {
    // 1. Programmatic while loop:
    //   B0 (preheader / entry) -> B1 (loop header)
    //   B1 -> B2 (loop body), B3 (exit)
    //   B2 -> B1 (back-edge)
    {
        phinale::ControlFlowGraph cfg;
        auto* b0 = cfg.create_block("entry");
        auto* b1 = cfg.create_block("header");
        auto* b2 = cfg.create_block("body");
        auto* b3 = cfg.create_block("exit");

        cfg.set_entry(b0);
        cfg.add_exit(b3);

        add_edge(b0, b1);
        add_edge(b1, b2);
        add_edge(b1, b3);
        add_edge(b2, b1); // back-edge

        phinale::DominanceAnalysis da(cfg);

        // Immediate dominators
        assert(da.get_idom(b0->id()) == b0->id());
        assert(da.get_idom(b1->id()) == b0->id());
        assert(da.get_idom(b2->id()) == b1->id());
        assert(da.get_idom(b3->id()) == b1->id());

        // Verify loop header dominates body
        assert(da.dominates(b1->id(), b2->id()));
        assert(da.strictly_dominates(b1->id(), b2->id()));

        // Verify loop header is in the dominance frontier of the latch/back-edge block
        assert(da.get_frontier(b2->id()).count(b1->id()) == 1);
    }

    // 2. End-to-end parsed while loop
    {
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

        // Find loop header block (the block ending with JUMP_IF)
        phinale::BasicBlock* header = nullptr;
        for (const auto& bb : cfg.blocks()) {
            if (!bb->instructions().empty() && bb->instructions().back().opcode == phinale::Opcode::JUMP_IF) {
                header = bb.get();
                break;
            }
        }
        assert(header != nullptr);
        assert(header->successors().size() == 2);

        auto* body = header->successors()[0];
        // Body block should have a back-edge to header
        bool has_back_edge = false;
        for (auto* succ : body->successors()) {
            if (succ == header) {
                has_back_edge = true;
            }
        }
        assert(has_back_edge);

        phinale::DominanceAnalysis da(cfg);

        // Verify loop header dominates body
        assert(da.dominates(header->id(), body->id()));
        assert(da.strictly_dominates(header->id(), body->id()));

        // Verify loop header is in the dominance frontier of the latch/back-edge block
        assert(da.get_frontier(body->id()).count(header->id()) == 1);
    }
}

int main() {
    test_straight_line();
    test_diamond_if_else();
    test_loop_while();

    std::cout << "All dominance tests passed successfully!\n";
    return 0;
}
