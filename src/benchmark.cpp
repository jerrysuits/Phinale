#include "phinale/benchmark.hpp"

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

#include <fstream>
#include <iomanip>
#include <sstream>

namespace phinale {

static std::size_t count_asm_lines(const std::string& asm_code) {
    std::size_t lines = 0;
    std::istringstream iss(asm_code);
    std::string line;
    while (std::getline(iss, line)) {
        auto start = line.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            lines++;
        }
    }
    return lines;
}

std::vector<BenchmarkKernel> BenchmarkRunner::default_kernels() {
    return {
        {
            "1_if_diamond",
            R"(int main() {
    int x = 10;
    int y = 20;
    int a = 0;
    int b = 0;
    if (x < y) {
        a = x + 5;
        b = y + 10;
    } else {
        a = x - 5;
        b = y - 10;
    }
    return a + b;
}
)"
        },
        {
            "2_fibonacci_iter",
            R"(int main() {
    int n = 10;
    int a = 0;
    int b = 1;
    int temp = 0;
    int i = 0;
    while (i < n) {
        temp = a + b;
        a = b;
        b = temp;
        i = i + 1;
    }
    return a;
}
)"
        },
        {
            "3_collatz_branch_loop",
            R"(int main() {
    int n = 15;
    int steps = 0;
    int acc_odd = 0;
    int acc_even = 0;
    while (n > 1) {
        if (n > 10) {
            acc_even = acc_even + n;
            n = n - 2;
        } else {
            acc_odd = acc_odd + n;
            n = n - 1;
        }
        steps = steps + 1;
    }
    return steps + acc_odd + acc_even;
}
)"
        },
        {
            "4_gcd_simul",
            R"(int main() {
    int a = 48;
    int b = 18;
    int count = 0;
    while (a != b) {
        if (a > b) {
            a = a - b;
        } else {
            b = b - a;
        }
        count = count + 1;
    }
    return a + count;
}
)"
        },
        {
            "5_high_pressure_accum",
            R"(int main() {
    int a = 1;
    int b = 2;
    int c = 3;
    int d = 4;
    int e = 5;
    int f = 6;
    int g = 7;
    int i = 0;
    int n = 10;
    while (i < n) {
        if (a < b) {
            a = a + 1;
            c = c + d;
            e = e + f;
        } else {
            b = b + 1;
            d = d + c;
            f = f + e;
        }
        g = g + a + b + c + d + e + f;
        i = i + 1;
    }
    return a + b + c + d + e + f + g;
}
)"
        }
    };
}

PipelineComparisonRow BenchmarkRunner::evaluate_kernel(const std::string& name,
                                                       const std::string& source,
                                                       std::size_t K) {
    PipelineComparisonRow row;
    row.benchmark_name = name;
    row.K = K;

    // RUN 1: BASELINE (Naive SSA Destruction Only)
    {
        Parser parser(source);
        auto ast = parser.parse_program();
        SemanticAnalyzer sema;
        sema.analyze(*ast);
        IRBuilder ir_builder;
        auto tac_prog = ir_builder.build_program(*ast);
        const auto& func = tac_prog.functions[0];
        CFGBuilder cfg_builder;
        auto cfg = cfg_builder.build(func);
        DominanceAnalysis dom(cfg);
        SSABuilder ssa(cfg, dom);
        NaiveSSADestructor destructor(cfg);
        row.phis_lowered = destructor.get_metrics().phi_count_eliminated;
        row.naive_copies_inserted = destructor.get_metrics().copies_inserted;

        LivenessAnalysis liveness(cfg);
        auto rig = build_interference_graph(cfg, liveness);
        row.baseline_vregs = rig.nodes().size();
        row.baseline_rig_edges = rig.edge_count();
        row.baseline_remaining_copies = rig.moves().size();

        ChaitinBriggsAllocator allocator;
        auto alloc = allocator.allocate(rig, K);
        row.baseline_phys_regs = alloc.metrics.physical_registers_used;
        row.baseline_spills = alloc.metrics.spilled_vregs;

        RISCVCodeGenerator codegen;
        std::string asm_code = codegen.generate(cfg, alloc, func.name);
        row.baseline_asm_lines = count_asm_lines(asm_code);
    }

    // RUN 2: OPTIMIZED (With Briggs' Conservative Coalescing)
    {
        Parser parser(source);
        auto ast = parser.parse_program();
        SemanticAnalyzer sema;
        sema.analyze(*ast);
        IRBuilder ir_builder;
        auto tac_prog = ir_builder.build_program(*ast);
        const auto& func = tac_prog.functions[0];
        CFGBuilder cfg_builder;
        auto cfg = cfg_builder.build(func);
        DominanceAnalysis dom(cfg);
        SSABuilder ssa(cfg, dom);
        NaiveSSADestructor destructor(cfg);
        LivenessAnalysis liveness(cfg);
        auto rig = build_interference_graph(cfg, liveness);

        ConservativeCoalescer coalescer(cfg, rig, K);
        row.coalesced_copies_eliminated = coalescer.get_metrics().copies_coalesced;

        LivenessAnalysis liveness_coalesced(cfg);
        auto rig_coalesced = build_interference_graph(cfg, liveness_coalesced);

        row.optimized_vregs = rig_coalesced.nodes().size();
        row.optimized_rig_edges = rig_coalesced.edge_count();
        row.optimized_remaining_copies = rig_coalesced.moves().size();

        ChaitinBriggsAllocator allocator;
        auto alloc = allocator.allocate(rig_coalesced, K);
        row.optimized_phys_regs = alloc.metrics.physical_registers_used;
        row.optimized_spills = alloc.metrics.spilled_vregs;

        RISCVCodeGenerator codegen;
        std::string asm_code = codegen.generate(cfg, alloc, func.name);
        row.optimized_asm_lines = count_asm_lines(asm_code);
    }

    if (row.baseline_remaining_copies > 0) {
        row.copy_elimination_rate_pct = 100.0 * static_cast<double>(row.baseline_remaining_copies - row.optimized_remaining_copies) / static_cast<double>(row.baseline_remaining_copies);
    } else {
        row.copy_elimination_rate_pct = 0.0;
    }

    if (row.baseline_asm_lines > 0) {
        row.asm_reduction_rate_pct = 100.0 * static_cast<double>(row.baseline_asm_lines - row.optimized_asm_lines) / static_cast<double>(row.baseline_asm_lines);
    } else {
        row.asm_reduction_rate_pct = 0.0;
    }

    return row;
}

std::vector<PipelineComparisonRow> BenchmarkRunner::run_all(const std::vector<std::size_t>& k_values) {
    std::vector<PipelineComparisonRow> rows;
    auto kernels = default_kernels();
    for (const auto& k_val : k_values) {
        for (const auto& kernel : kernels) {
            rows.push_back(evaluate_kernel(kernel.name, kernel.source, k_val));
        }
    }
    return rows;
}

std::string BenchmarkRunner::format_markdown_table(const std::vector<PipelineComparisonRow>& rows) {
    std::ostringstream ss;
    ss << "| Benchmark | K | Phis | Naive Copies | Elim Copies | Base Rem Copies | Opt Rem Copies | Copy Elim % | Base VRegs | Opt VRegs | Base Edges | Opt Edges | Base Regs | Opt Regs | Base Spills | Opt Spills | Base Asm | Opt Asm | Asm Red % |\n";
    ss << "|:---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";

    for (const auto& r : rows) {
        ss << "| `" << r.benchmark_name << "` "
           << "| " << r.K << " "
           << "| " << r.phis_lowered << " "
           << "| " << r.naive_copies_inserted << " "
           << "| " << r.coalesced_copies_eliminated << " "
           << "| " << r.baseline_remaining_copies << " "
           << "| " << r.optimized_remaining_copies << " "
           << "| " << std::fixed << std::setprecision(1) << r.copy_elimination_rate_pct << "% "
           << "| " << r.baseline_vregs << " "
           << "| " << r.optimized_vregs << " "
           << "| " << r.baseline_rig_edges << " "
           << "| " << r.optimized_rig_edges << " "
           << "| " << r.baseline_phys_regs << " "
           << "| " << r.optimized_phys_regs << " "
           << "| " << r.baseline_spills << " "
           << "| " << r.optimized_spills << " "
           << "| " << r.baseline_asm_lines << " "
           << "| " << r.optimized_asm_lines << " "
           << "| " << std::fixed << std::setprecision(1) << r.asm_reduction_rate_pct << "% |\n";
    }

    return ss.str();
}

bool BenchmarkRunner::export_csv(const std::vector<PipelineComparisonRow>& rows, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    file << "benchmark,K,phis_lowered,naive_copies_inserted,coalesced_copies_eliminated,baseline_remaining_copies,optimized_remaining_copies,copy_elimination_rate_pct,baseline_vregs,optimized_vregs,baseline_rig_edges,optimized_rig_edges,baseline_phys_regs,optimized_phys_regs,baseline_spills,optimized_spills,baseline_asm_lines,optimized_asm_lines,asm_reduction_rate_pct\n";

    for (const auto& r : rows) {
        file << r.benchmark_name << ","
             << r.K << ","
             << r.phis_lowered << ","
             << r.naive_copies_inserted << ","
             << r.coalesced_copies_eliminated << ","
             << r.baseline_remaining_copies << ","
             << r.optimized_remaining_copies << ","
             << std::fixed << std::setprecision(2) << r.copy_elimination_rate_pct << ","
             << r.baseline_vregs << ","
             << r.optimized_vregs << ","
             << r.baseline_rig_edges << ","
             << r.optimized_rig_edges << ","
             << r.baseline_phys_regs << ","
             << r.optimized_phys_regs << ","
             << r.baseline_spills << ","
             << r.optimized_spills << ","
             << r.baseline_asm_lines << ","
             << r.optimized_asm_lines << ","
             << std::fixed << std::setprecision(2) << r.asm_reduction_rate_pct << "\n";
    }

    return true;
}

} // namespace phinale
