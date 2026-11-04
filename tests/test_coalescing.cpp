#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/coalescing.hpp"
#include "phinale/dominance.hpp"
#include "phinale/interference_graph.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/liveness.hpp"
#include "phinale/parser.hpp"
#include "phinale/sema.hpp"
#include "phinale/ssa_builder.hpp"
#include "phinale/ssa_destruction.hpp"

#include <cassert>
#include <iostream>
#include <string>

static std::size_t count_cfg_instructions(const phinale::ControlFlowGraph& cfg) {
    std::size_t total = 0;
    for (const auto& bb_ptr : cfg.blocks()) {
        if (bb_ptr) {
            total += bb_ptr->instructions().size();
        }
    }
    return total;
}

void test_safe_copy_coalescing() {
    // Test 1 (Safe Copy Coalescing):
    // Construct a CFG with v0 = 10; v1 = COPY v0; return v1;
    // where v0 and v1 do not interfere and degree is well below K.
    // Verify copies_coalesced == 1, the COPY instruction is eliminated from the CFG,
    // v1 is rewritten to v0, and rig.nodes().size() decreased by 1.
    phinale::ControlFlowGraph cfg;
    auto* b0 = cfg.create_block("b0");
    cfg.set_entry(b0);
    cfg.add_exit(b0);

    phinale::VReg v0{0};
    phinale::VReg v1{1};

    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v0,
        phinale::Operand::from_int(10),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v1,
        phinale::Operand::from_vreg(v0),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(v1),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::LivenessAnalysis liveness(cfg);
    auto rig = phinale::build_interference_graph(cfg, liveness);

    assert(rig.nodes().size() == 2);
    assert(!rig.interferes(v0, v1));
    assert(rig.moves().size() == 1);

    std::size_t initial_nodes = rig.nodes().size();
    phinale::ConservativeCoalescer coalescer(cfg, rig, 27);
    const auto& metrics = coalescer.get_metrics();

    assert(metrics.copies_coalesced == 1);
    assert(rig.nodes().size() == initial_nodes - 1);
    assert(rig.nodes().find(v0) != rig.nodes().end());
    assert(rig.nodes().find(v1) == rig.nodes().end());

    // Verify COPY instruction eliminated and v1 rewritten to v0
    assert(b0->instructions().size() == 2);
    assert(b0->instructions()[0].opcode == phinale::Opcode::COPY);
    assert(b0->instructions()[0].dest.has_value() && *b0->instructions()[0].dest == v0);

    assert(b0->instructions()[1].opcode == phinale::Opcode::RETURN);
    assert(b0->instructions()[1].src1.has_value() && b0->instructions()[1].src1->is_vreg());
    assert(b0->instructions()[1].src1->as_vreg() == v0);

    std::cout << "  ✓ Test 1: Safe copy coalescing passed\n";
}

void test_interference_rejection() {
    // Test 2 (Interference Rejection):
    // Construct a CFG where v1 = COPY v0, but v0 is subsequently mutated or stays
    // live across a competing definition of v1 so rig.interferes(v0, v1) == true.
    // Verify ConservativeCoalescer does NOT coalesce v0 and v1,
    // copies_rejected_interfering >= 1, and the COPY instruction remains intact in the CFG.
    phinale::ControlFlowGraph cfg;
    auto* b0 = cfg.create_block("b0");
    cfg.set_entry(b0);
    cfg.add_exit(b0);

    phinale::VReg v0{0};
    phinale::VReg v1{1};
    phinale::VReg v2{2};

    // v0 = 10
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v0,
        phinale::Operand::from_int(10),
        std::nullopt, std::nullopt, std::nullopt
    });
    // v1 = COPY v0
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v1,
        phinale::Operand::from_vreg(v0),
        std::nullopt, std::nullopt, std::nullopt
    });
    // v0 = 20 (competing redefinition of v0 while v1 is still live)
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v0,
        phinale::Operand::from_int(20),
        std::nullopt, std::nullopt, std::nullopt
    });
    // v2 = v0 + v1 (both v0 and v1 are live simultaneously)
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::ADD,
        v2,
        phinale::Operand::from_vreg(v0),
        phinale::Operand::from_vreg(v1),
        std::nullopt, std::nullopt
    });
    // return v2
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(v2),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::LivenessAnalysis liveness(cfg);
    auto rig = phinale::build_interference_graph(cfg, liveness);

    assert(rig.interferes(v0, v1));

    std::size_t initial_inst_count = b0->instructions().size();
    phinale::ConservativeCoalescer coalescer(cfg, rig, 27);
    const auto& metrics = coalescer.get_metrics();

    assert(metrics.copies_coalesced == 0);
    assert(metrics.copies_rejected_interfering >= 1);

    // Verify copy instruction remains intact
    assert(b0->instructions().size() == initial_inst_count);
    assert(b0->instructions()[1].opcode == phinale::Opcode::COPY);
    assert(b0->instructions()[1].dest.has_value() && *b0->instructions()[1].dest == v1);
    assert(b0->instructions()[1].src1.has_value() && b0->instructions()[1].src1->as_vreg() == v0);

    // Verify both nodes remain in rig
    assert(rig.nodes().find(v0) != rig.nodes().end());
    assert(rig.nodes().find(v1) != rig.nodes().end());

    std::cout << "  ✓ Test 2: Interference rejection passed\n";
}

void test_briggs_criterion_rejection() {
    // Test 3 (Briggs' Criterion Rejection under High Register Pressure K=2):
    // Construct an interference graph with K = 2 where u and v are connected by a COPY
    // (and do not interfere with each other), but u interferes with {n1, n2}
    // (where n1, n2 also have other neighbors so their post-merge degree is >= 2).
    // Verify can_coalesce_briggs(u, v, 2) returns false, copies_rejected_briggs == 1,
    // and u and v remain separate nodes.
    phinale::ControlFlowGraph cfg;
    auto* b0 = cfg.create_block("b0");
    cfg.set_entry(b0);

    phinale::VReg u{0};
    phinale::VReg v{1};
    phinale::VReg n1{2};
    phinale::VReg n2{3};
    phinale::VReg m1{4};
    phinale::VReg m2{5};

    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        u,
        phinale::Operand::from_vreg(v),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::InterferenceGraph rig;
    rig.add_node(u);
    rig.add_node(v);
    rig.add_node(n1);
    rig.add_node(n2);
    rig.add_node(m1);
    rig.add_node(m2);

    rig.moves().push_back(phinale::CopyEdge{u, v});

    rig.add_edge(u, n1);
    rig.add_edge(u, n2);
    rig.add_edge(n1, m1);
    rig.add_edge(n2, m2);

    assert(!rig.interferes(u, v));
    assert(!phinale::ConservativeCoalescer::can_coalesce_briggs(rig, u, v, 2));

    phinale::ConservativeCoalescer coalescer(cfg, rig, 2);
    assert(!coalescer.can_coalesce_briggs(u, v, 2));

    const auto& metrics = coalescer.get_metrics();
    assert(metrics.copies_rejected_briggs == 1);
    assert(metrics.copies_coalesced == 0);

    // Verify u and v remain separate nodes
    assert(rig.nodes().find(u) != rig.nodes().end());
    assert(rig.nodes().find(v) != rig.nodes().end());

    std::cout << "  ✓ Test 3: Briggs criterion rejection (K=2) passed\n";
}

void test_end_to_end_diamond_and_loop_coalescing() {
    // Test 4 (End-to-End SSA Diamond & Loop Coalescing):
    // Run full pipeline: AST -> TAC -> CFG -> Dominance -> SSA -> NaiveSSADestructor
    // -> Liveness -> InterferenceGraph -> ConservativeCoalescer.
    // Verify that redundant copies introduced by NaiveSSADestructor at if-else branch ends
    // are coalesced away and total CFG instruction count decreases compared to the naive baseline.

    // 1. If-Else Diamond
    {
        std::string src =
            "int compute(int a, int b) {\n"
            "    int res = 0;\n"
            "    if (a > b) {\n"
            "        res = a + 1;\n"
            "    } else {\n"
            "        res = b + 2;\n"
            "    }\n"
            "    return res;\n"
            "}\n";

        phinale::Parser parser(src);
        auto program = parser.parse_program();
        assert(program != nullptr);

        phinale::SemanticAnalyzer sema;
        sema.analyze(*program);

        phinale::IRBuilder ir_builder;
        auto tac_prog = ir_builder.build_program(*program);
        assert(!tac_prog.functions.empty());

        phinale::CFGBuilder cfg_builder;
        auto cfg = cfg_builder.build(tac_prog.functions[0]);

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);
        phinale::NaiveSSADestructor destructor(cfg);
        assert(destructor.get_metrics().copies_inserted >= 2);

        std::size_t instructions_before = count_cfg_instructions(cfg);

        phinale::LivenessAnalysis liveness(cfg);
        auto rig = phinale::build_interference_graph(cfg, liveness);
        assert(!rig.moves().empty());

        phinale::ConservativeCoalescer coalescer(cfg, rig, 27);
        const auto& metrics = coalescer.get_metrics();

        assert(metrics.copies_coalesced >= 1);
        std::size_t instructions_after = count_cfg_instructions(cfg);
        assert(instructions_after < instructions_before);
    }

    // 2. Loop with accumulator
    {
        std::string loop_src =
            "int sum(int n) {\n"
            "    int i = 0;\n"
            "    int acc = 0;\n"
            "    while (i < n) {\n"
            "        acc = acc + i;\n"
            "        i = i + 1;\n"
            "    }\n"
            "    return acc;\n"
            "}\n";

        phinale::Parser parser(loop_src);
        auto program = parser.parse_program();
        assert(program != nullptr);

        phinale::SemanticAnalyzer sema;
        sema.analyze(*program);

        phinale::IRBuilder ir_builder;
        auto tac_prog = ir_builder.build_program(*program);
        assert(!tac_prog.functions.empty());

        phinale::CFGBuilder cfg_builder;
        auto cfg = cfg_builder.build(tac_prog.functions[0]);

        phinale::DominanceAnalysis dom(cfg);
        phinale::SSABuilder ssa(cfg, dom);
        phinale::NaiveSSADestructor destructor(cfg);

        std::size_t instructions_before = count_cfg_instructions(cfg);

        phinale::LivenessAnalysis liveness(cfg);
        auto rig = phinale::build_interference_graph(cfg, liveness);

        phinale::ConservativeCoalescer coalescer(cfg, rig, 27);
        const auto& metrics = coalescer.get_metrics();

        assert(metrics.copies_coalesced >= 1);
        std::size_t instructions_after = count_cfg_instructions(cfg);
        assert(instructions_after < instructions_before);
    }

    std::cout << "  ✓ Test 4: End-to-end SSA diamond and loop coalescing passed\n";
}

int main() {
    std::cout << "Running Conservative Copy Coalescing unit tests...\n";
    test_safe_copy_coalescing();
    test_interference_rejection();
    test_briggs_criterion_rejection();
    test_end_to_end_diamond_and_loop_coalescing();

    std::cout << "All conservative copy coalescing tests passed successfully!\n";
    return 0;
}
