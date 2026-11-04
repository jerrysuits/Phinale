#include "phinale/regalloc.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

namespace phinale {

std::string ChaitinBriggsAllocator::physical_reg_name(int color) {
    if (color >= 0 && color <= 6) {
        return "t" + std::to_string(color);
    }
    if (color >= 7 && color <= 14) {
        return "a" + std::to_string(color - 7);
    }
    if (color >= 15 && color <= 25) {
        return "s" + std::to_string(color - 15 + 1);
    }
    if (color == 26) {
        return "ra";
    }
    if (color < 0) {
        return "spill";
    }
    return "r" + std::to_string(color);
}

AllocationResult ChaitinBriggsAllocator::allocate(const InterferenceGraph& rig, std::size_t K) {
    AllocationResult result;
    auto work_adj = rig.nodes();
    result.metrics.total_vregs = work_adj.size();

    if (work_adj.empty() || K == 0) {
        return result;
    }

    // Simplify & Spill Loop
    std::vector<std::pair<VReg, bool>> select_stack;

    while (!work_adj.empty()) {
        std::optional<VReg> simplify_node = std::nullopt;
        for (const auto& [node, neighbors] : work_adj) {
            if (neighbors.size() < K) {
                if (!simplify_node.has_value() || node < *simplify_node) {
                    simplify_node = node;
                }
            }
        }

        if (simplify_node.has_value()) {
            VReg v = *simplify_node;
            select_stack.emplace_back(v, false);
            for (const auto& nbr : work_adj[v]) {
                work_adj[nbr].erase(v);
            }
            work_adj.erase(v);
        } else {
            // Potential spill: pick node with maximum degree (tie-break deterministically)
            std::optional<VReg> spill_node = std::nullopt;
            std::size_t max_deg = 0;
            for (const auto& [node, neighbors] : work_adj) {
                std::size_t deg = neighbors.size();
                if (!spill_node.has_value() || deg > max_deg || (deg == max_deg && node < *spill_node)) {
                    max_deg = deg;
                    spill_node = node;
                }
            }

            VReg v = *spill_node;
            select_stack.emplace_back(v, true);
            for (const auto& nbr : work_adj[v]) {
                work_adj[nbr].erase(v);
            }
            work_adj.erase(v);
        }
    }

    // Select Phase (LIFO order)
    while (!select_stack.empty()) {
        auto [v, _] = select_stack.back();
        select_stack.pop_back();

        std::unordered_set<int> neighbor_colors;
        for (const auto& nbr : rig.neighbors(v)) {
            auto it = result.color_map.find(nbr);
            if (it != result.color_map.end() && it->second >= 0) {
                neighbor_colors.insert(it->second);
            }
        }

        int chosen_color = -1;
        for (std::size_t c = 0; c < K; ++c) {
            if (neighbor_colors.find(static_cast<int>(c)) == neighbor_colors.end()) {
                chosen_color = static_cast<int>(c);
                break;
            }
        }

        if (chosen_color == -1) {
            // Actual spill
            result.color_map[v] = -1;
            int offset = -4 * static_cast<int>(result.metrics.spilled_vregs + 1);
            result.spill_offset_map[v] = offset;
            result.metrics.spilled_vregs++;
        } else {
            result.color_map[v] = chosen_color;
            result.metrics.colored_vregs++;
        }
    }

    // Metrics calculation
    std::unordered_set<int> unique_colors;
    for (const auto& [_, col] : result.color_map) {
        if (col >= 0) {
            unique_colors.insert(col);
        }
    }
    result.metrics.physical_registers_used = unique_colors.size();

    if (result.metrics.spilled_vregs > 0) {
        std::size_t raw_bytes = result.metrics.spilled_vregs * 4;
        result.metrics.stack_frame_bytes = ((raw_bytes + 15) / 16) * 16;
    } else {
        result.metrics.stack_frame_bytes = 0;
    }

    return result;
}

} // namespace phinale
