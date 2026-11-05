#include "phinale/benchmark.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

void test_copy_reduction_invariant() {
    // Test 1 (Non-Negative Copy Reduction Invariant):
    // Across all 5 kernels at K=26, verify optimized_remaining_copies <= baseline_remaining_copies
    // and copy_elimination_rate_pct > 50.0.
    phinale::BenchmarkRunner runner;
    auto kernels = phinale::BenchmarkRunner::default_kernels();
    assert(kernels.size() == 5);

    for (const auto& kernel : kernels) {
        auto row = runner.evaluate_kernel(kernel.name, kernel.source, 26);
        assert(row.optimized_remaining_copies <= row.baseline_remaining_copies);
        assert(row.copy_elimination_rate_pct > 50.0);
        assert(row.coalesced_copies_eliminated > 0);
    }

    std::cout << "  ✓ Test 1: Non-negative copy reduction invariant (K=26) passed\n";
}

void test_briggs_spill_safety_guarantee() {
    // Test 2 (Briggs Spill Safety Guarantee):
    // Across all 5 kernels at tight register pressure (K=4 and K=5), verify that Briggs'
    // conservative coalescing NEVER introduces more spills than the naive baseline
    // (optimized_spills <= baseline_spills), proving empirically that Briggs' criterion preserves K-colorability.
    phinale::BenchmarkRunner runner;
    auto kernels = phinale::BenchmarkRunner::default_kernels();

    for (std::size_t K : {4, 5}) {
        for (const auto& kernel : kernels) {
            auto row = runner.evaluate_kernel(kernel.name, kernel.source, K);
            assert(row.optimized_spills <= row.baseline_spills);
        }
    }

    std::cout << "  ✓ Test 2: Briggs spill safety guarantee (K=4, K=5) passed\n";
}

void test_assembly_bloat_reduction() {
    // Test 3 (Assembly Instruction Bloat Reduction):
    // Verify optimized_asm_lines < baseline_asm_lines across all kernels at standard register budget K=26.
    phinale::BenchmarkRunner runner;
    auto kernels = phinale::BenchmarkRunner::default_kernels();

    for (const auto& kernel : kernels) {
        auto row = runner.evaluate_kernel(kernel.name, kernel.source, 26);
        assert(row.optimized_asm_lines < row.baseline_asm_lines);
        assert(row.asm_reduction_rate_pct > 0.0);
    }

    std::cout << "  ✓ Test 3: Assembly instruction bloat reduction passed\n";
}

void test_csv_and_markdown_export() {
    // Test 4 (CSV & Markdown Export):
    // Verify format_markdown_table and export_csv produce valid, non-empty output.
    phinale::BenchmarkRunner runner;
    auto rows = runner.run_all({26, 4});
    assert(rows.size() == 10); // 5 kernels * 2 K values

    std::string md = phinale::BenchmarkRunner::format_markdown_table(rows);
    assert(!md.empty());
    assert(md.find("| Benchmark | K |") != std::string::npos);
    assert(md.find("1_if_diamond") != std::string::npos);
    assert(md.find("5_high_pressure_accum") != std::string::npos);

    std::string test_csv_file = "test_export.csv";
    bool success = phinale::BenchmarkRunner::export_csv(rows, test_csv_file);
    assert(success);

    std::ifstream in(test_csv_file);
    assert(in.is_open());
    std::string header;
    std::getline(in, header);
    assert(header.find("benchmark,K,") != std::string::npos);
    size_t line_count = 1;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            line_count++;
        }
    }
    assert(line_count == 11); // header + 10 rows
    in.close();
    std::remove(test_csv_file.c_str());

    std::cout << "  ✓ Test 4: CSV and Markdown export passed\n";
}

int main() {
    std::cout << "Running Benchmark Suite & Empirical Invariants unit tests...\n";
    test_copy_reduction_invariant();
    test_briggs_spill_safety_guarantee();
    test_assembly_bloat_reduction();
    test_csv_and_markdown_export();

    std::cout << "All benchmark tests passed successfully!\n";
    return 0;
}
