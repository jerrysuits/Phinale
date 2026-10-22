#include "phinale/ast.hpp"
#include "phinale/cfg.hpp"
#include "phinale/cfg_builder.hpp"
#include "phinale/dominance.hpp"
#include "phinale/interference_graph.hpp"
#include "phinale/ir_builder.hpp"
#include "phinale/lexer.hpp"
#include "phinale/liveness.hpp"
#include "phinale/parser.hpp"
#include "phinale/sema.hpp"
#include "phinale/ssa_builder.hpp"
#include "phinale/ssa_destruction.hpp"
#include "phinale/tac.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static const char* const DEMO_SOURCE = R"(int main() {
    int a = 1;
    int b = 10;
    int res = 0;
    while (a < b) {
        if (a == 5) {
            res = res + a;
        } else {
            res = res + 2;
        }
        a = a + 1;
    }
    return res;
}
)";

static std::string format_vreg(const phinale::VReg& v) {
    if (v.version.has_value()) {
        return "v" + std::to_string(v.id) + "_" + std::to_string(*v.version);
    }
    return "v" + std::to_string(v.id);
}

static std::string format_vreg_set(const std::unordered_set<phinale::VReg>& s) {
    if (s.empty()) return "{}";
    std::vector<phinale::VReg> sorted(s.begin(), s.end());
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream oss;
    oss << "{";
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << format_vreg(sorted[i]);
    }
    oss << "}";
    return oss.str();
}

static std::string format_block_set(const std::unordered_set<phinale::BlockID>& s) {
    if (s.empty()) return "{}";
    std::vector<phinale::BlockID> sorted(s.begin(), s.end());
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream oss;
    oss << "{";
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "B" << sorted[i];
    }
    oss << "}";
    return oss.str();
}

static void print_cfg_summary(const phinale::ControlFlowGraph& cfg) {
    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;
        std::cout << "  BasicBlock B" << bb_ptr->id();
        if (bb_ptr->label().has_value()) {
            std::cout << " [" << *bb_ptr->label() << "]";
        }
        std::cout << ":\n";

        // Predecessors
        std::cout << "    Predecessors: [";
        for (std::size_t i = 0; i < bb_ptr->predecessors().size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << "B" << bb_ptr->predecessors()[i]->id();
        }
        std::cout << "]\n";

        // Successors
        std::cout << "    Successors:   [";
        for (std::size_t i = 0; i < bb_ptr->successors().size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << "B" << bb_ptr->successors()[i]->id();
        }
        std::cout << "]\n";

        // Phis
        for (const auto& phi : bb_ptr->phis) {
            std::cout << "    " << phinale::to_string(phi) << "\n";
        }

        // Instructions
        for (const auto& inst : bb_ptr->instructions()) {
            std::cout << "  " << phinale::to_string(inst) << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    std::string source_code;
    bool has_file = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--demo" || arg == "--all" || arg == "-h" || arg == "--help") {
            continue;
        }
        if (!arg.empty() && arg[0] != '-') {
            std::ifstream file(arg);
            if (!file.is_open()) {
                std::cerr << "Error: Could not open file: " << arg << "\n";
                return 1;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            source_code = buffer.str();
            has_file = true;
            break;
        }
    }

    if (!has_file) {
        source_code = DEMO_SOURCE;
    }

    std::cout << "=======================================================\n";
    std::cout << "               PHINALE COMPILER PIPELINE               \n";
    std::cout << "=======================================================\n\n";

    // STAGE 1: LEXER & PARSER
    std::cout << "[STAGE 1: LEXER & PARSER]\n";
    phinale::Parser parser(source_code);
    auto program_ast = parser.parse_program();
    if (!program_ast) {
        std::cerr << "Error: Parsing failed to produce an AST.\n";
        return 1;
    }
    phinale::SemanticAnalyzer sema;
    sema.analyze(*program_ast);
    std::cout << "  ✓ Lexical and syntactic analysis complete.\n";
    std::cout << "  ✓ Program AST parsed with " << program_ast->functions.size() << " function(s).\n";
    std::cout << "  ✓ Semantic validation passed successfully.\n\n";

    // STAGE 2: THREE-ADDRESS CODE (TAC)
    std::cout << "[STAGE 2: THREE-ADDRESS CODE (TAC)]\n";
    phinale::IRBuilder ir_builder;
    auto tac_prog = ir_builder.build_program(*program_ast);
    std::cout << phinale::to_string(tac_prog) << "\n";

    if (tac_prog.functions.empty()) {
        std::cout << "No functions found in TAC program.\n";
        return 0;
    }

    const auto& main_func = tac_prog.functions[0];

    // STAGE 3: CONTROL FLOW GRAPH (CFG)
    std::cout << "[STAGE 3: CONTROL FLOW GRAPH (CFG)]\n";
    phinale::CFGBuilder cfg_builder;
    auto cfg = cfg_builder.build(main_func);
    print_cfg_summary(cfg);
    std::cout << "\n";

    // STAGE 4: DOMINANCE ANALYSIS
    std::cout << "[STAGE 4: DOMINANCE ANALYSIS]\n";
    phinale::DominanceAnalysis dom(cfg);
    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;
        phinale::BlockID bid = bb_ptr->id();
        std::cout << "  B" << bid << ": idom = B" << dom.get_idom(bid)
                  << ", DF = " << format_block_set(dom.get_frontier(bid)) << "\n";
    }
    std::cout << "\n";

    // STAGE 5: SSA CONSTRUCTION
    std::cout << "[STAGE 5: SSA CONSTRUCTION]\n";
    phinale::SSABuilder ssa_builder(cfg, dom);
    print_cfg_summary(cfg);

    {
        std::ofstream dot_file("cfg_ssa.dot");
        if (dot_file.is_open()) {
            cfg.dump_dot(dot_file);
            std::cout << "  ✓ Wrote SSA CFG to cfg_ssa.dot\n";
        } else {
            std::cerr << "  ✗ Warning: Failed to open cfg_ssa.dot for writing.\n";
        }
    }
    std::cout << "\n";

    // STAGE 6: NAIVE SSA DESTRUCTION
    std::cout << "[STAGE 6: NAIVE SSA DESTRUCTION]\n";
    phinale::NaiveSSADestructor destructor(cfg);
    const auto& metrics = destructor.get_metrics();
    std::cout << "  Metrics:\n";
    std::cout << "    Phi nodes eliminated:   " << metrics.phi_count_eliminated << "\n";
    std::cout << "    Copy instructions added: " << metrics.copies_inserted << "\n";
    std::cout << "    Critical edges split:   " << metrics.critical_edges_split << "\n\n";
    std::cout << "  De-SSA CFG:\n";
    print_cfg_summary(cfg);

    {
        std::ofstream dot_file("cfg.dot");
        if (dot_file.is_open()) {
            cfg.dump_dot(dot_file);
            std::cout << "  ✓ Wrote De-SSA CFG to cfg.dot\n";
        } else {
            std::cerr << "  ✗ Warning: Failed to open cfg.dot for writing.\n";
        }
    }
    std::cout << "\n";

    // STAGE 7: BACKWARD LIVENESS ANALYSIS
    std::cout << "[STAGE 7: BACKWARD LIVENESS ANALYSIS]\n";
    phinale::LivenessAnalysis liveness(cfg);
    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;
        phinale::BlockID bid = bb_ptr->id();
        std::cout << "  B" << bid << ":\n";
        std::cout << "    Use:     " << format_vreg_set(liveness.get_use(bid)) << "\n";
        std::cout << "    Def:     " << format_vreg_set(liveness.get_def(bid)) << "\n";
        std::cout << "    LiveIn:  " << format_vreg_set(liveness.get_live_in(bid)) << "\n";
        std::cout << "    LiveOut: " << format_vreg_set(liveness.get_live_out(bid)) << "\n";
    }
    std::cout << "\n";

    // STAGE 8: REGISTER INTERFERENCE GRAPH (RIG)
    std::cout << "[STAGE 8: REGISTER INTERFERENCE GRAPH (RIG)]\n";
    auto rig = phinale::build_interference_graph(cfg, liveness);
    std::cout << "  Total Nodes:       " << rig.nodes().size() << "\n";
    std::cout << "  Interference Edges: " << rig.edge_count() << "\n";
    std::cout << "  Max Degree:        " << rig.max_degree() << "\n";
    std::cout << "  Move Candidates:   " << rig.moves().size() << "\n\n";

    std::cout << "  Nodes & Adjacencies:\n";
    std::vector<phinale::VReg> sorted_nodes;
    sorted_nodes.reserve(rig.nodes().size());
    for (const auto& [node, _] : rig.nodes()) {
        sorted_nodes.push_back(node);
    }
    std::sort(sorted_nodes.begin(), sorted_nodes.end());

    for (const auto& node : sorted_nodes) {
        std::cout << "    " << format_vreg(node) << " (deg=" << rig.degree(node) << "): "
                  << format_vreg_set(rig.neighbors(node)) << "\n";
    }

    if (!rig.moves().empty()) {
        std::cout << "\n  Move/Copy Preferences:\n";
        for (const auto& move : rig.moves()) {
            std::cout << "    " << format_vreg(move.dst) << " <- " << format_vreg(move.src) << "\n";
        }
    }

    {
        std::ofstream rig_file("rig.dot");
        if (rig_file.is_open()) {
            rig_file << rig.dump_dot();
            std::cout << "\n  ✓ Wrote Interference Graph to rig.dot\n";
        } else {
            std::cerr << "\n  ✗ Warning: Failed to open rig.dot for writing.\n";
        }
    }

    std::cout << "\n=======================================================\n";
    std::cout << "               PIPELINE COMPLETED OK                   \n";
    std::cout << "=======================================================\n";

    return 0;
}
