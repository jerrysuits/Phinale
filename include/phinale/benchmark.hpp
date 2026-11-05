#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace phinale {

struct PipelineComparisonRow {
    std::string benchmark_name;
    std::size_t K{0}; // target physical register budget
    std::size_t phis_lowered{0};
    std::size_t naive_copies_inserted{0};
    std::size_t coalesced_copies_eliminated{0};
    std::size_t baseline_remaining_copies{0};
    std::size_t optimized_remaining_copies{0};
    std::size_t baseline_vregs{0};
    std::size_t optimized_vregs{0};
    std::size_t baseline_rig_edges{0};
    std::size_t optimized_rig_edges{0};
    std::size_t baseline_phys_regs{0};
    std::size_t optimized_phys_regs{0};
    std::size_t baseline_spills{0};
    std::size_t optimized_spills{0};
    std::size_t baseline_asm_lines{0};
    std::size_t optimized_asm_lines{0};
    double copy_elimination_rate_pct{0.0};
    double asm_reduction_rate_pct{0.0};
};

struct BenchmarkKernel {
    std::string name;
    std::string source;
};

class BenchmarkRunner {
public:
    static std::vector<BenchmarkKernel> default_kernels();

    PipelineComparisonRow evaluate_kernel(const std::string& name, const std::string& source, std::size_t K);

    std::vector<PipelineComparisonRow> run_all(const std::vector<std::size_t>& k_values = {26, 4});

    static std::string format_markdown_table(const std::vector<PipelineComparisonRow>& rows);

    static bool export_csv(const std::vector<PipelineComparisonRow>& rows, const std::string& filename);
};

} // namespace phinale
