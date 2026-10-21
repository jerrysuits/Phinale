#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/dominance.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/parser.hpp"
#include "phinale/ssa_builder.hpp"
#include "phinale/ssa_destruction.hpp"

#include <cassert>
#include <iostream>
#include <string>

static void add_edge(phinale::BasicBlock* from, phinale::BasicBlock* to) {
    if (!from || !to) return;
    from->add_successor(to);
    to->add_predecessor(from);
}

void test_diamond_destruction() {
    // Test 1 (Diamond CFG):
    // Construct diamond CFG with a phi node at the merge point.
    // Execute NaiveSSADestructor.
    // Verify CFG has zero remaining phi nodes.
    // Verify that incoming predecessor paths contain the expected COPY instructions with matching operands.
    // Verify metrics: phi_count_eliminated == 1, copies_inserted == 2.
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

    // b0 ends with conditional jump
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP_IF,
        std::nullopt,
        phinale::Operand::from_bool(true),
        std::nullopt,
        "then", "else"
    });

    // b1: v1 = 100, goto join
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        phinale::VReg{1},
        phinale::Operand::from_int(100),
        std::nullopt, std::nullopt, std::nullopt
    });
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "join", std::nullopt
    });

    // b2: v2 = 200, goto join
    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        phinale::VReg{2},
        phinale::Operand::from_int(200),
        std::nullopt, std::nullopt, std::nullopt
    });
    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "join", std::nullopt
    });

    // b3: phi v3 = [b1: v1], [b2: v2]
    phinale::PhiNode phi;
    phi.dest = phinale::VReg{3};
    phi.add_incoming(b1->id(), phinale::Operand::from_vreg(phinale::VReg{1}));
    phi.add_incoming(b2->id(), phinale::Operand::from_vreg(phinale::VReg{2}));
    b3->add_phi(std::move(phi));

    b3->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(phinale::VReg{3}),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::NaiveSSADestructor destructor(cfg);
    const auto& metrics = destructor.get_metrics();

    // Verify CFG has zero remaining phi nodes
    for (const auto& bb : cfg.blocks()) {
        assert(!bb->has_phi());
        assert(bb->phis.empty());
    }

    // Verify metrics
    assert(metrics.phi_count_eliminated == 1);
    assert(metrics.copies_inserted == 2);
    assert(metrics.critical_edges_split == 0);

    // Verify that incoming predecessor paths contain the expected COPY instructions with matching operands
    // In b1: COPY v3 = v1 immediately before the JUMP
    assert(b1->instructions().size() == 3);
    const auto& b1_copy = b1->instructions()[1];
    assert(b1_copy.opcode == phinale::Opcode::COPY);
    assert(b1_copy.dest.has_value() && b1_copy.dest->id == 3);
    assert(b1_copy.src1.has_value() && b1_copy.src1->is_vreg() && b1_copy.src1->as_vreg().id == 1);

    // In b2: COPY v3 = v2 immediately before the JUMP
    assert(b2->instructions().size() == 3);
    const auto& b2_copy = b2->instructions()[1];
    assert(b2_copy.opcode == phinale::Opcode::COPY);
    assert(b2_copy.dest.has_value() && b2_copy.dest->id == 3);
    assert(b2_copy.src1.has_value() && b2_copy.src1->is_vreg() && b2_copy.src1->as_vreg().id == 2);
}

void test_loop_destruction() {
    // Test 2 (Loop / Back-edge):
    // Construct a loop with a loop-header phi.
    // Execute NaiveSSADestructor.
    // Verify copy inserted in the preheader/entry and in the latch/loop-body block.
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

    // b0: v0 = 0, goto header
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        phinale::VReg{0},
        phinale::Operand::from_int(0),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "header", std::nullopt
    });

    // b1: phi v3 = [b0: v0], [b2: v2]
    phinale::PhiNode phi;
    phi.dest = phinale::VReg{3};
    phi.add_incoming(b0->id(), phinale::Operand::from_vreg(phinale::VReg{0}));
    phi.add_incoming(b2->id(), phinale::Operand::from_vreg(phinale::VReg{2}));
    b1->add_phi(std::move(phi));

    // cond = v3 < 10, if cond goto body else goto exit
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::CMP_LT,
        phinale::VReg{4},
        phinale::Operand::from_vreg(phinale::VReg{3}),
        phinale::Operand::from_int(10),
        std::nullopt, std::nullopt
    });
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP_IF,
        std::nullopt,
        phinale::Operand::from_vreg(phinale::VReg{4}),
        std::nullopt,
        "body", "exit"
    });

    // b2: v2 = v3 + 1, goto header
    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::ADD,
        phinale::VReg{2},
        phinale::Operand::from_vreg(phinale::VReg{3}),
        phinale::Operand::from_int(1),
        std::nullopt, std::nullopt
    });
    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "header", std::nullopt
    });

    // b3: return v3
    b3->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(phinale::VReg{3}),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::NaiveSSADestructor destructor(cfg);
    const auto& metrics = destructor.get_metrics();

    // Verify zero remaining phi nodes
    for (const auto& bb : cfg.blocks()) {
        assert(!bb->has_phi());
        assert(bb->phis.empty());
    }

    assert(metrics.phi_count_eliminated == 1);
    assert(metrics.copies_inserted == 2);

    // Verify copy inserted in the preheader/entry (b0) immediately before JUMP
    bool found_b0_copy = false;
    for (const auto& inst : b0->instructions()) {
        if (inst.opcode == phinale::Opcode::COPY && inst.dest.has_value() && inst.dest->id == 3 &&
            inst.src1.has_value() && inst.src1->is_vreg() && inst.src1->as_vreg().id == 0) {
            found_b0_copy = true;
        }
    }
    assert(found_b0_copy);

    // Verify copy inserted in the latch/loop-body block (b2) immediately before JUMP
    bool found_b2_copy = false;
    for (const auto& inst : b2->instructions()) {
        if (inst.opcode == phinale::Opcode::COPY && inst.dest.has_value() && inst.dest->id == 3 &&
            inst.src1.has_value() && inst.src1->is_vreg() && inst.src1->as_vreg().id == 2) {
            found_b2_copy = true;
        }
    }
    assert(found_b2_copy);
}

void test_critical_edge_destruction() {
    // Test 3 (Critical edge split):
    // Test a branch with multiple successors leading to a block with multiple predecessors.
    // Edge B0 -> B2 is critical:
    // B0 has 2 successors (B1, B2)
    // B2 has 2 predecessors (B0, B1)
    // Verify a trampoline block is allocated and wired with correct predecessor/successor edges.
    phinale::ControlFlowGraph cfg;
    auto* b0 = cfg.create_block("branch");
    auto* b1 = cfg.create_block("then");
    auto* b2 = cfg.create_block("join");

    cfg.set_entry(b0);
    cfg.add_exit(b2);

    add_edge(b0, b1);
    add_edge(b0, b2); // Critical edge!
    add_edge(b1, b2);

    // b0 ends with JUMP_IF targeting b1 (then) and b2 (join)
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP_IF,
        std::nullopt,
        phinale::Operand::from_bool(true),
        std::nullopt,
        "then", "join"
    });

    // b1: v1 = 10, goto join
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        phinale::VReg{1},
        phinale::Operand::from_int(10),
        std::nullopt, std::nullopt, std::nullopt
    });
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "join", std::nullopt
    });

    // b2: phi v3 = [b0: v0], [b1: v1]
    phinale::PhiNode phi;
    phi.dest = phinale::VReg{3};
    phi.add_incoming(b0->id(), phinale::Operand::from_vreg(phinale::VReg{0}));
    phi.add_incoming(b1->id(), phinale::Operand::from_vreg(phinale::VReg{1}));
    b2->add_phi(std::move(phi));

    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(phinale::VReg{3}),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::NaiveSSADestructor destructor(cfg);
    const auto& metrics = destructor.get_metrics();

    // Verify zero remaining phi nodes
    for (const auto& bb : cfg.blocks()) {
        assert(!bb->has_phi());
        assert(bb->phis.empty());
    }

    // Verify metrics
    assert(metrics.critical_edges_split == 1);
    assert(metrics.phi_count_eliminated == 1);
    assert(metrics.copies_inserted == 2);

    // Find the trampoline block
    phinale::BasicBlock* trampoline = nullptr;
    for (const auto& bb : cfg.blocks()) {
        if (bb->label().has_value() && bb->label()->find("trampoline") != std::string::npos) {
            trampoline = bb.get();
            break;
        }
    }
    assert(trampoline != nullptr);

    // Verify trampoline is wired correctly:
    // Predecessor is B0
    assert(trampoline->predecessors().size() == 1);
    assert(trampoline->predecessors()[0] == b0);

    // Successor is B2
    assert(trampoline->successors().size() == 1);
    assert(trampoline->successors()[0] == b2);

    // B0's successors should include trampoline and NOT b2
    bool b0_has_b2 = false;
    bool b0_has_trampoline = false;
    for (auto* succ : b0->successors()) {
        if (succ == b2) b0_has_b2 = true;
        if (succ == trampoline) b0_has_trampoline = true;
    }
    assert(!b0_has_b2);
    assert(b0_has_trampoline);

    // B2's predecessors should include trampoline and NOT b0
    bool b2_has_b0 = false;
    bool b2_has_trampoline = false;
    for (auto* pred : b2->predecessors()) {
        if (pred == b0) b2_has_b0 = true;
        if (pred == trampoline) b2_has_trampoline = true;
    }
    assert(!b2_has_b0);
    assert(b2_has_trampoline);

    // Verify copy instruction was placed inside the trampoline block
    bool copy_in_trampoline = false;
    for (const auto& inst : trampoline->instructions()) {
        if (inst.opcode == phinale::Opcode::COPY && inst.dest.has_value() && inst.dest->id == 3 &&
            inst.src1.has_value() && inst.src1->is_vreg() && inst.src1->as_vreg().id == 0) {
            copy_in_trampoline = true;
        }
    }
    assert(copy_in_trampoline);

    // Verify copy instruction for b1 was placed inside b1
    bool copy_in_b1 = false;
    for (const auto& inst : b1->instructions()) {
        if (inst.opcode == phinale::Opcode::COPY && inst.dest.has_value() && inst.dest->id == 3 &&
            inst.src1.has_value() && inst.src1->is_vreg() && inst.src1->as_vreg().id == 1) {
            copy_in_b1 = true;
        }
    }
    assert(copy_in_b1);
}

void test_end_to_end_pipeline() {
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

    // SSA constructed: join block has phi
    bool had_phi = false;
    for (const auto& bb : cfg.blocks()) {
        if (bb->has_phi()) had_phi = true;
    }
    assert(had_phi);

    // Now destruct SSA
    phinale::NaiveSSADestructor destructor(cfg);
    for (const auto& bb : cfg.blocks()) {
        assert(!bb->has_phi());
    }

    assert(destructor.get_metrics().phi_count_eliminated >= 1);
    assert(destructor.get_metrics().copies_inserted >= 2);
}

int main() {
    test_diamond_destruction();
    test_loop_destruction();
    test_critical_edge_destruction();
    test_end_to_end_pipeline();

    std::cout << "All SSA destruction tests passed successfully!\n";
    return 0;
}
