#pragma once

#include "phinale/interference_graph.hpp"
#include "phinale/tac.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace phinale {

struct AllocationMetrics {
    std::size_t total_vregs = 0;
    std::size_t colored_vregs = 0;
    std::size_t spilled_vregs = 0;
    std::size_t physical_registers_used = 0;
    std::size_t stack_frame_bytes = 0;
};

struct AllocationResult {
    std::unordered_map<VReg, int> color_map;
    std::unordered_map<VReg, int> spill_offset_map;
    AllocationMetrics metrics;
};

class ChaitinBriggsAllocator {
public:
    static std::string physical_reg_name(int color);

    AllocationResult allocate(const InterferenceGraph& rig, std::size_t K = 26);
};

} // namespace phinale
