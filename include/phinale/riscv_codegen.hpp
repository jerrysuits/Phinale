#pragma once

#include "phinale/cfg.hpp"
#include "phinale/regalloc.hpp"

#include <string>

namespace phinale {

class RISCVCodeGenerator {
public:
    std::string generate(const ControlFlowGraph& cfg, const AllocationResult& alloc, const std::string& func_name = "main") const;
};

} // namespace phinale
