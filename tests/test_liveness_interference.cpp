#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/dominance.hpp"
#include "phinale/interference_graph.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/liveness.hpp"
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

void test_disjoint_live_ranges() {
    // Test 1 (Disjoint Live Ranges):
    // v0 = 10; use(v0) via return or dummy; then v1 = 20; use(v1).
    // Specifically:
    // v0 = 10
    // v2 = COPY v0  (v0 dies here)
    // v1 = 20
    // return v1     (v1 dies here)
    // Verify v0 and v1 do NOT interfere.
    phinale::ControlFlowGraph cfg;
    auto* b0 = cfg.create_block("b0");
    cfg.set_entry(b0);
    cfg.add_exit(b0);

    phinale::VReg v0{0};
    phinale::VReg v1{1};
    phinale::VReg v2{2};

    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v0,
        phinale::Operand::from_int(10),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v2,
        phinale::Operand::from_vreg(v0),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v1,
        phinale::Operand::from_int(20),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(v1),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::LivenessAnalysis liveness(cfg);
    auto graph = phinale::build_interference_graph(cfg, liveness);

    assert(!graph.interferes(v0, v1));
    assert(!graph.interferes(v1, v0));
}

void test_overlapping_live_ranges() {
    // Test 2 (Overlapping Live Ranges):
    // v0 = 10; v1 = 20; v2 = v0 + v1; return v2;
    // Verify v0 and v1 interfere.
    phinale::ControlFlowGraph cfg;
    auto* b0 = cfg.create_block("b0");
    cfg.set_entry(b0);
    cfg.add_exit(b0);

    phinale::VReg v0{0};
    phinale::VReg v1{1};
    phinale::VReg v2{2};

    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v0,
        phinale::Operand::from_int(10),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v1,
        phinale::Operand::from_int(20),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::ADD,
        v2,
        phinale::Operand::from_vreg(v0),
        phinale::Operand::from_vreg(v1),
        std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(v2),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::LivenessAnalysis liveness(cfg);
    auto graph = phinale::build_interference_graph(cfg, liveness);

    assert(graph.interferes(v0, v1));
    assert(graph.interferes(v1, v0));
    assert(graph.degree(v0) >= 1);
    assert(graph.degree(v1) >= 1);
}

void test_copy_instruction_exemption() {
    // Test 3 (Copy Instruction Exemption):
    // v0 = 10; v1 = COPY v0; return v1; (where v0 is dead after the copy).
    // Verify v0 and v1 do NOT interfere, and {v1, v0} is recorded in graph.moves().
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
    auto graph = phinale::build_interference_graph(cfg, liveness);

    // v0 and v1 must NOT interfere
    assert(!graph.interferes(v0, v1));
    assert(!graph.interferes(v1, v0));

    // Verify {v1, v0} is recorded in moves
    bool found_move = false;
    for (const auto& m : graph.moves()) {
        if (m.dst == v1 && m.src == v0) {
            found_move = true;
            break;
        }
    }
    assert(found_move);
}

void test_loop_carried_liveness() {
    // Test 4 (Loop-Carried Liveness):
    // Construct a loop where v0 is defined before the loop and used inside the loop body
    // alongside a loop-local variable v1.
    // Verify v0 is in live_in and live_out across the loop header/body and interferes with v1.
    //
    // B0 (preheader): v0 = 10; goto B1
    // B1 (header): cond = v0 > 0; if cond goto B2 else goto B3
    // B2 (body): v1 = 42; dummy = v1 + v0; goto B1
    // B3 (exit): return v0
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

    phinale::VReg v0{0};
    phinale::VReg v1{1};
    phinale::VReg v_cond{2};
    phinale::VReg v_dummy{3};

    // B0: v0 = 10, goto header
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v0,
        phinale::Operand::from_int(10),
        std::nullopt, std::nullopt, std::nullopt
    });
    b0->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "header", std::nullopt
    });

    // B1: cond = v0 > 0, if cond goto body else goto exit
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::CMP_GT,
        v_cond,
        phinale::Operand::from_vreg(v0),
        phinale::Operand::from_int(0),
        std::nullopt, std::nullopt
    });
    b1->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP_IF,
        std::nullopt,
        phinale::Operand::from_vreg(v_cond),
        std::nullopt,
        "body", "exit"
    });

    // B2: v1 = 42, dummy = v1 + v0, goto header
    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::COPY,
        v1,
        phinale::Operand::from_int(42),
        std::nullopt, std::nullopt, std::nullopt
    });
    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::ADD,
        v_dummy,
        phinale::Operand::from_vreg(v1),
        phinale::Operand::from_vreg(v0),
        std::nullopt, std::nullopt
    });
    b2->instructions().push_back(phinale::Instruction{
        phinale::Opcode::JUMP,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "header", std::nullopt
    });

    // B3: return v0
    b3->instructions().push_back(phinale::Instruction{
        phinale::Opcode::RETURN,
        std::nullopt,
        phinale::Operand::from_vreg(v0),
        std::nullopt, std::nullopt, std::nullopt
    });

    phinale::LivenessAnalysis liveness(cfg);

    // Verify v0 is in live_in and live_out across the loop header (b1) and body (b2)
    assert(liveness.get_live_in(b1->id()).count(v0) == 1);
    assert(liveness.get_live_out(b1->id()).count(v0) == 1);
    assert(liveness.get_live_in(b2->id()).count(v0) == 1);
    assert(liveness.get_live_out(b2->id()).count(v0) == 1);

    auto graph = phinale::build_interference_graph(cfg, liveness);

    // Verify v0 interferes with v1
    assert(graph.interferes(v0, v1));
    assert(graph.interferes(v1, v0));
}

void test_end_to_end_pipeline_and_dot() {
    // Test 5 (End-to-End Pipeline & DOT Export):
    // Run AST -> TAC -> CFG -> SSA -> Naive SSA Destruction -> Liveness -> InterferenceGraph.
    // Verify dump_dot() produces non-empty valid DOT syntax containing nodes and edges.
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

    phinale::IRBuilder ir_builder;
    auto tac_prog = ir_builder.build_program(*program);
    assert(tac_prog.functions.size() == 1);

    phinale::CFGBuilder cfg_builder;
    auto cfg = cfg_builder.build(tac_prog.functions[0]);

    phinale::DominanceAnalysis dom(cfg);
    phinale::SSABuilder ssa(cfg, dom);
    phinale::NaiveSSADestructor destructor(cfg);

    phinale::LivenessAnalysis liveness(cfg);
    auto graph = phinale::build_interference_graph(cfg, liveness);

    std::string dot = graph.dump_dot();

    assert(!dot.empty());
    assert(dot.find("graph InterferenceGraph {") != std::string::npos);
    assert(dot.find("node [shape=circle") != std::string::npos);
    assert(dot.find("v") != std::string::npos);
    assert(dot.find("}") != std::string::npos);

    // Ensure edge count and max_degree query methods work
    assert(graph.edge_count() >= 1);
    assert(graph.max_degree() >= 1);
}

int main() {
    test_disjoint_live_ranges();
    test_overlapping_live_ranges();
    test_copy_instruction_exemption();
    test_loop_carried_liveness();
    test_end_to_end_pipeline_and_dot();

    std::cout << "All liveness and interference graph tests passed successfully!\n";
    return 0;
}
