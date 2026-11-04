#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/coalescing.hpp"
#include "phinale/dominance.hpp"
#include "phinale/interference_graph.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/liveness.hpp"
#include "phinale/parser.hpp"
#include "phinale/regalloc.hpp"
#include "phinale/riscv_codegen.hpp"
#include "phinale/sema.hpp"
#include "phinale/ssa_builder.hpp"
#include "phinale/ssa_destruction.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <unordered_set>

void test_conflict_free_coloring() {
    // Test 1 (Conflict-Free Coloring):
    // Verify non-interfering variables can share the same physical register color 0 (t0).
    phinale::InterferenceGraph rig;
    phinale::VReg v0{0};
    phinale::VReg v1{1};

    rig.add_node(v0);
    rig.add_node(v1);

    assert(!rig.interferes(v0, v1));

    phinale::ChaitinBriggsAllocator allocator;
    auto alloc = allocator.allocate(rig, 26);

    assert(alloc.metrics.total_vregs == 2);
    assert(alloc.metrics.colored_vregs == 2);
    assert(alloc.metrics.spilled_vregs == 0);
    assert(alloc.metrics.physical_registers_used == 1);

    assert(alloc.color_map.at(v0) == 0);
    assert(alloc.color_map.at(v1) == 0);

    assert(phinale::ChaitinBriggsAllocator::physical_reg_name(0) == "t0");
    assert(alloc.metrics.stack_frame_bytes == 0);

    std::cout << "  ✓ Test 1: Conflict-free coloring passed\n";
}

void test_clique_coloring() {
    // Test 2 (Clique Coloring K=3):
    // Construct a 3-clique (v0, v1, v2 mutually interfering) with K=3;
    // verify all 3 receive distinct colors {0, 1, 2} and spilled_vregs == 0.
    phinale::InterferenceGraph rig;
    phinale::VReg v0{0};
    phinale::VReg v1{1};
    phinale::VReg v2{2};

    rig.add_node(v0);
    rig.add_node(v1);
    rig.add_node(v2);

    rig.add_edge(v0, v1);
    rig.add_edge(v1, v2);
    rig.add_edge(v0, v2);

    assert(rig.interferes(v0, v1));
    assert(rig.interferes(v1, v2));
    assert(rig.interferes(v0, v2));

    phinale::ChaitinBriggsAllocator allocator;
    auto alloc = allocator.allocate(rig, 3);

    assert(alloc.metrics.total_vregs == 3);
    assert(alloc.metrics.colored_vregs == 3);
    assert(alloc.metrics.spilled_vregs == 0);
    assert(alloc.metrics.physical_registers_used == 3);

    int c0 = alloc.color_map.at(v0);
    int c1 = alloc.color_map.at(v1);
    int c2 = alloc.color_map.at(v2);

    assert(c0 >= 0 && c0 < 3);
    assert(c1 >= 0 && c1 < 3);
    assert(c2 >= 0 && c2 < 3);

    assert(c0 != c1);
    assert(c1 != c2);
    assert(c0 != c2);

    std::cout << "  ✓ Test 2: Clique coloring (K=3) passed\n";
}

void test_forced_spill_high_pressure() {
    // Test 3 (Forced Spill under High Pressure K=2):
    // Run the same 3-clique with K=2;
    // verify 2 nodes are colored and 1 node is spilled (spilled_vregs == 1) with a valid stack offset.
    phinale::InterferenceGraph rig;
    phinale::VReg v0{0};
    phinale::VReg v1{1};
    phinale::VReg v2{2};

    rig.add_node(v0);
    rig.add_node(v1);
    rig.add_node(v2);

    rig.add_edge(v0, v1);
    rig.add_edge(v1, v2);
    rig.add_edge(v0, v2);

    phinale::ChaitinBriggsAllocator allocator;
    auto alloc = allocator.allocate(rig, 2);

    assert(alloc.metrics.total_vregs == 3);
    assert(alloc.metrics.colored_vregs == 2);
    assert(alloc.metrics.spilled_vregs == 1);
    assert(alloc.metrics.physical_registers_used == 2);
    assert(alloc.metrics.stack_frame_bytes >= 16);

    int spilled_count = 0;
    phinale::VReg spilled_node{0};
    for (const auto& [node, color] : alloc.color_map) {
        if (color == -1) {
            spilled_count++;
            spilled_node = node;
        } else {
            assert(color >= 0 && color < 2);
        }
    }

    assert(spilled_count == 1);
    assert(alloc.spill_offset_map.find(spilled_node) != alloc.spill_offset_map.end());
    assert(alloc.spill_offset_map.at(spilled_node) < 0);

    std::cout << "  ✓ Test 3: Forced spill under high pressure (K=2) passed\n";
}

void test_end_to_end_riscv_codegen() {
    // Test 4 (End-to-End RISC-V Assembly Generation):
    // Run the full pipeline through RISCVCodeGenerator and verify the emitted assembly
    // contains .globl main, loop labels, arithmetic instructions, and ret.
    std::string src =
        "int main() {\n"
        "    int a = 1;\n"
        "    int b = 10;\n"
        "    int res = 0;\n"
        "    while (a < b) {\n"
        "        res = res + a;\n"
        "        a = a + 1;\n"
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

    phinale::LivenessAnalysis liveness(cfg);
    auto rig = phinale::build_interference_graph(cfg, liveness);

    phinale::ConservativeCoalescer coalescer(cfg, rig, 27);

    // Recompute liveness on coalesced CFG
    phinale::LivenessAnalysis liveness_coalesced(cfg);
    auto rig_coalesced = phinale::build_interference_graph(cfg, liveness_coalesced);

    phinale::ChaitinBriggsAllocator allocator;
    auto alloc = allocator.allocate(rig_coalesced, 26);

    assert(alloc.metrics.colored_vregs > 0);
    assert(alloc.metrics.spilled_vregs == 0);

    phinale::RISCVCodeGenerator codegen;
    std::string asm_code = codegen.generate(cfg, alloc, "main");

    assert(!asm_code.empty());
    assert(asm_code.find(".globl main") != std::string::npos);
    assert(asm_code.find("main:") != std::string::npos);
    assert(asm_code.find("ret") != std::string::npos);
    assert(asm_code.find("add") != std::string::npos || asm_code.find("addi") != std::string::npos);
    assert(asm_code.find("L_loop") != std::string::npos || asm_code.find("L_") != std::string::npos);

    std::cout << "  ✓ Test 4: End-to-end RISC-V codegen passed\n";
}

int main() {
    std::cout << "Running Register Allocation & RISC-V Codegen unit tests...\n";
    test_conflict_free_coloring();
    test_clique_coloring();
    test_forced_spill_high_pressure();
    test_end_to_end_riscv_codegen();

    std::cout << "All register allocation and codegen tests passed successfully!\n";
    return 0;
}
