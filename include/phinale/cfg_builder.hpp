#pragma once

#include "phinale/cfg.hpp"
#include "phinale/tac.h"

namespace phinale {

class CFGBuilder {
public:
    CFGBuilder() = default;

    ControlFlowGraph build(const TACFunction& function);
};

} // namespace phinale
