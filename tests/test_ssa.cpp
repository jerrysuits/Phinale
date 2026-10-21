#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/dominance.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/parser.hpp"
#include "phinale/ssa_builder.hpp"

#include <cassert>
#include <iostream>
#include <string>

static void add_edge(phinale::BasicBlock* from, phinale::BasicBlock* to) {
    if (!from || !to) return;
    from->add_successor(to);
    to->add_predecessor(from);
}

void test_straight_line() {
    // 1. Programmatic straight-line code:
    // v0 = 10
    // v1 = 20
    // v2 = v0 + v1
    // return v2
    {
        phinale::ControlFlowGraph cfg;
        auto* b0 = cfg.create_block("b0");
        cfg.set_entry(b0);
        cfg.add_exit(b0);

        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{0},
            phinale::Operand::from_int(10),
            std::nullopt, std::nullopt, std::nullopt
        });
        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{1},
            phinale::Operand::from_int(20),
            std::nullopt, std::nullopt, std::nullopt
        });
        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::ADD,
            phinale::VReg{2},
            phinale::Operand::from_vreg(phinale::VReg{0}),
            phinale::Operand::from_vreg(phinale::VReg{1}),
            std::nullopt, std::nullopt
        });
        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::RETURN,
            std::nullopt,
            phinale::Operand::from_vreg(phinale::VReg{2}),
            std::nullopt, std::nullopt, std::nullopt
        });

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);

        // Verify no phi nodes inserted
        assert(!b0->has_phi());
        assert(b0->phis.empty());

        // Verify registers are renamed linearly
        const auto& insts = b0->instructions();
        assert(insts.size() == 4);

        assert(insts[0].dest.has_value());
        assert(insts[0].dest->id == 0);

        assert(insts[1].dest.has_value());
        assert(insts[1].dest->id == 1);

        assert(insts[2].dest.has_value());
        assert(insts[2].dest->id == 2);

        // Verify RHS uses refer to the renamed defs
        assert(insts[2].src1.has_value() && insts[2].src1->is_vreg());
        assert(insts[2].src1->as_vreg().id == 0);

        assert(insts[2].src2.has_value() && insts[2].src2->is_vreg());
        assert(insts[2].src2->as_vreg().id == 1);

        assert(insts[3].src1.has_value() && insts[3].src1->is_vreg());
        assert(insts[3].src1->as_vreg().id == 2);
    }

    // 2. Programmatic straight-line re-assignment:
    // v0 = 1
    // v0 = 2
    // return v0
    {
        phinale::ControlFlowGraph cfg;
        auto* b0 = cfg.create_block("b0");
        cfg.set_entry(b0);
        cfg.add_exit(b0);

        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{0},
            phinale::Operand::from_int(1),
            std::nullopt, std::nullopt, std::nullopt
        });
        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{0},
            phinale::Operand::from_int(2),
            std::nullopt, std::nullopt, std::nullopt
        });
        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::RETURN,
            std::nullopt,
            phinale::Operand::from_vreg(phinale::VReg{0}),
            std::nullopt, std::nullopt, std::nullopt
        });

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);

        assert(!b0->has_phi());
        const auto& insts = b0->instructions();
        assert(insts[0].dest->id == 0);
        assert(insts[0].dest->version == 0);

        assert(insts[1].dest->id == 1);
        assert(insts[1].dest->version == 1);

        // Return should use the second definition (v1)
        assert(insts[2].src1->as_vreg().id == 1);
        assert(insts[2].src1->as_vreg().version == 1);
    }

    // 3. Parsed straight-line program
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

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);

        for (const auto& bb : cfg.blocks()) {
            assert(!bb->has_phi());
        }
    }
}

void test_diamond_if_else() {
    // 1. Programmatic Diamond CFG:
    //      B0 (branch block)
    //     /  \ (split)
    //   B1    B2 (then/else, both assign variable v0)
    //     \  /
    //      B3 (join block, returns v0)
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

        // b0 has conditional jump
        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::JUMP_IF,
            std::nullopt,
            phinale::Operand::from_bool(true),
            std::nullopt,
            "then", "else"
        });

        // b1 assigns v0 = 100
        b1->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{0},
            phinale::Operand::from_int(100),
            std::nullopt, std::nullopt, std::nullopt
        });

        // b2 assigns v0 = 200
        b2->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{0},
            phinale::Operand::from_int(200),
            std::nullopt, std::nullopt, std::nullopt
        });

        // b3 returns v0
        b3->instructions().push_back(phinale::Instruction{
            phinale::Opcode::RETURN,
            std::nullopt,
            phinale::Operand::from_vreg(phinale::VReg{0}),
            std::nullopt, std::nullopt, std::nullopt
        });

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);

        // Verify neither branch arms nor entry have phis
        assert(!b0->has_phi());
        assert(!b1->has_phi());
        assert(!b2->has_phi());

        // Verify a single phi node is placed in the join block
        assert(b3->has_phi());
        assert(b3->phis.size() == 1);

        const auto& phi = b3->phis[0];
        // Verify incoming operand versions correspond correctly to their respective branches
        auto in_then = phi.get_incoming(b1->id());
        auto in_else = phi.get_incoming(b2->id());

        assert(in_then.has_value() && in_then->is_vreg());
        assert(in_else.has_value() && in_else->is_vreg());

        // Both versions must be distinct
        assert(in_then->as_vreg() != in_else->as_vreg());

        // Version from b1 matches b1's dest
        assert(b1->instructions()[0].dest.has_value());
        assert(b1->instructions()[0].dest.value() == in_then->as_vreg());

        // Version from b2 matches b2's dest
        assert(b2->instructions()[0].dest.has_value());
        assert(b2->instructions()[0].dest.value() == in_else->as_vreg());

        // b3 return uses the phi destination
        assert(b3->instructions()[0].src1.has_value());
        assert(b3->instructions()[0].src1->as_vreg() == phi.dest);
    }

    // 2. Parsed if-else with variable assigned in both branches
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

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);

        // Find join block (the one returning res)
        assert(!cfg.exits().empty());
        auto* join = cfg.exits()[0];
        assert(join->has_phi());
        assert(!join->phis.empty());

        // The return instruction in join block should use the phi destination
        assert(!join->instructions().empty());
        assert(join->instructions().back().opcode == phinale::Opcode::RETURN);
        assert(join->instructions().back().src1.has_value());
        assert(join->instructions().back().src1->is_vreg());
        assert(join->instructions().back().src1->as_vreg() == join->phis[0].dest);
    }
}

void test_loop_while() {
    // 1. Programmatic while loop:
    // B0 (preheader): i = 0
    // B1 (header): cond = i < 10; if cond goto B2 else goto B3
    // B2 (body): t = i + 1; i = t; goto B1
    // B3 (exit): return i
    {
        phinale::ControlFlowGraph cfg;
        auto* b0 = cfg.create_block("preheader");
        auto* b1 = cfg.create_block("header");
        auto* b2 = cfg.create_block("body");
        auto* b3 = cfg.create_block("exit");

        cfg.set_entry(b0);
        cfg.add_exit(b3);

        add_edge(b0, b1);
        add_edge(b1, b2);
        add_edge(b1, b3);
        add_edge(b2, b1); // back-edge

        // B0: i = 0
        b0->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{0},
            phinale::Operand::from_int(0),
            std::nullopt, std::nullopt, std::nullopt
        });

        // B1: cond = i < 10; if cond goto B2 else goto B3
        b1->instructions().push_back(phinale::Instruction{
            phinale::Opcode::CMP_LT,
            phinale::VReg{1},
            phinale::Operand::from_vreg(phinale::VReg{0}),
            phinale::Operand::from_int(10),
            std::nullopt, std::nullopt
        });
        b1->instructions().push_back(phinale::Instruction{
            phinale::Opcode::JUMP_IF,
            std::nullopt,
            phinale::Operand::from_vreg(phinale::VReg{1}),
            std::nullopt,
            "body", "exit"
        });

        // B2: t = i + 1; i = t
        b2->instructions().push_back(phinale::Instruction{
            phinale::Opcode::ADD,
            phinale::VReg{2},
            phinale::Operand::from_vreg(phinale::VReg{0}),
            phinale::Operand::from_int(1),
            std::nullopt, std::nullopt
        });
        b2->instructions().push_back(phinale::Instruction{
            phinale::Opcode::COPY,
            phinale::VReg{0},
            phinale::Operand::from_vreg(phinale::VReg{2}),
            std::nullopt, std::nullopt, std::nullopt
        });

        // B3: return i
        b3->instructions().push_back(phinale::Instruction{
            phinale::Opcode::RETURN,
            std::nullopt,
            phinale::Operand::from_vreg(phinale::VReg{0}),
            std::nullopt, std::nullopt, std::nullopt
        });

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);

        // Verify loop header receives a phi node
        assert(b1->has_phi());
        assert(b1->phis.size() == 1);

        const auto& phi = b1->phis[0];
        // Merges entry version and loop-carried back-edge version
        auto in_entry = phi.get_incoming(b0->id());
        auto in_latch = phi.get_incoming(b2->id());

        assert(in_entry.has_value() && in_entry->is_vreg());
        assert(in_latch.has_value() && in_latch->is_vreg());

        // Verify entry version is version 0
        assert(in_entry->as_vreg().version.has_value());
        assert(in_entry->as_vreg().version.value() == 0);

        // Verify loop-carried back-edge version is different from entry version
        assert(in_latch->as_vreg().version.has_value());
        assert(in_latch->as_vreg().version.value() > 0);
        assert(in_entry->as_vreg() != in_latch->as_vreg());

        // Verify header condition uses the phi destination
        assert(b1->instructions()[0].src1.has_value());
        assert(b1->instructions()[0].src1->as_vreg() == phi.dest);

        // Verify latch definition matches in_latch
        assert(b2->instructions()[1].dest.has_value());
        assert(b2->instructions()[1].dest.value() == in_latch->as_vreg());

        // Verify exit block returns the phi destination
        assert(b3->instructions()[0].src1.has_value());
        assert(b3->instructions()[0].src1->as_vreg() == phi.dest);
    }

    // 2. Parsed while loop
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

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);

        // Find loop header block (the one with JUMP_IF)
        phinale::BasicBlock* header = nullptr;
        for (const auto& bb : cfg.blocks()) {
            if (!bb->instructions().empty() && bb->instructions().back().opcode == phinale::Opcode::JUMP_IF) {
                header = bb.get();
                break;
            }
        }
        assert(header != nullptr);
        // Header must have phi node for i
        assert(header->has_phi());
        assert(!header->phis.empty());
    }
}

int main() {
    test_straight_line();
    test_diamond_if_else();
    test_loop_while();

    std::cout << "All SSA tests passed successfully!\n";
    return 0;
}
