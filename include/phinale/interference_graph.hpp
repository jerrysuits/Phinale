#pragma once

#include "phinale/cfg.hpp"
#include "phinale/liveness.hpp"
#include "phinale/tac.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phinale {

struct CopyEdge {
    VReg dst;
    VReg src;

    bool operator==(const CopyEdge& other) const noexcept {
        return dst == other.dst && src == other.src;
    }

    bool operator!=(const CopyEdge& other) const noexcept {
        return !(*this == other);
    }
};

class InterferenceGraph {
public:
    InterferenceGraph() = default;

    void add_node(VReg v);
    void add_edge(VReg u, VReg v);
    bool interferes(VReg u, VReg v) const;
    std::size_t degree(VReg v) const;
    const std::unordered_set<VReg>& neighbors(VReg v) const;
    const std::unordered_map<VReg, std::unordered_set<VReg>>& nodes() const noexcept { return adj_; }
    const std::vector<CopyEdge>& moves() const noexcept { return move_list_; }
    std::vector<CopyEdge>& moves() noexcept { return move_list_; }
    void set_moves(std::vector<CopyEdge> moves) { move_list_ = std::move(moves); }
    void merge_nodes(VReg keep, VReg remove);
    std::size_t edge_count() const noexcept;
    std::size_t max_degree() const noexcept;

    std::string dump_dot() const;

    static InterferenceGraph build(const ControlFlowGraph& cfg, const LivenessAnalysis& liveness);

private:
    std::unordered_map<VReg, std::unordered_set<VReg>> adj_;
    std::vector<CopyEdge> move_list_;
};

InterferenceGraph build_interference_graph(const ControlFlowGraph& cfg, const LivenessAnalysis& liveness);

} // namespace phinale
