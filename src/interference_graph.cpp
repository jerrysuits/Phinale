#include "phinale/interference_graph.hpp"

#include <algorithm>
#include <sstream>

namespace phinale {

void InterferenceGraph::add_node(VReg v) {
    if (adj_.find(v) == adj_.end()) {
        adj_[v] = {};
    }
}

void InterferenceGraph::add_edge(VReg u, VReg v) {
    if (u == v) return;
    add_node(u);
    add_node(v);
    adj_[u].insert(v);
    adj_[v].insert(u);
}

bool InterferenceGraph::interferes(VReg u, VReg v) const {
    auto it = adj_.find(u);
    if (it != adj_.end()) {
        return it->second.find(v) != it->second.end();
    }
    return false;
}

std::size_t InterferenceGraph::degree(VReg v) const {
    auto it = adj_.find(v);
    if (it != adj_.end()) {
        return it->second.size();
    }
    return 0;
}

const std::unordered_set<VReg>& InterferenceGraph::neighbors(VReg v) const {
    auto it = adj_.find(v);
    if (it != adj_.end()) {
        return it->second;
    }
    static const std::unordered_set<VReg> empty;
    return empty;
}

void InterferenceGraph::merge_nodes(VReg keep, VReg remove) {
    if (keep == remove) return;
    add_node(keep);
    auto it = adj_.find(remove);
    if (it == adj_.end()) return;

    auto remove_neighbors = std::move(it->second);
    adj_.erase(it);

    for (const auto& w : remove_neighbors) {
        adj_[w].erase(remove);
        if (w != keep) {
            adj_[keep].insert(w);
            adj_[w].insert(keep);
        }
    }
}

std::size_t InterferenceGraph::edge_count() const noexcept {
    std::size_t total = 0;
    for (const auto& [_, nbrs] : adj_) {
        total += nbrs.size();
    }
    return total / 2;
}

std::size_t InterferenceGraph::max_degree() const noexcept {
    std::size_t max_d = 0;
    for (const auto& [_, nbrs] : adj_) {
        if (nbrs.size() > max_d) {
            max_d = nbrs.size();
        }
    }
    return max_d;
}

std::string InterferenceGraph::dump_dot() const {
    std::ostringstream ss;
    ss << "graph InterferenceGraph {\n";
    ss << "  node [shape=circle, fontname=\"Courier\"];\n";

    // Deterministic order for nodes
    std::vector<VReg> sorted_nodes;
    sorted_nodes.reserve(adj_.size());
    for (const auto& [node, _] : adj_) {
        sorted_nodes.push_back(node);
    }
    std::sort(sorted_nodes.begin(), sorted_nodes.end());

    auto vreg_str = [](const VReg& v) -> std::string {
        if (v.version.has_value()) {
            return "v" + std::to_string(v.id) + "_" + std::to_string(*v.version);
        }
        return "v" + std::to_string(v.id);
    };

    for (const auto& node : sorted_nodes) {
        ss << "  " << vreg_str(node) << ";\n";
    }

    // Emit interference edges (u < v to avoid duplicates)
    for (const auto& u : sorted_nodes) {
        auto it = adj_.find(u);
        if (it == adj_.end()) continue;
        std::vector<VReg> sorted_nbrs(it->second.begin(), it->second.end());
        std::sort(sorted_nbrs.begin(), sorted_nbrs.end());
        for (const auto& v : sorted_nbrs) {
            if (u < v) {
                ss << "  " << vreg_str(u) << " -- " << vreg_str(v) << ";\n";
            }
        }
    }

    // Emit move/copy preference edges
    for (const auto& move : move_list_) {
        ss << "  " << vreg_str(move.dst) << " -- " << vreg_str(move.src)
           << " [style=dashed, color=blue];\n";
    }

    ss << "}\n";
    return ss.str();
}

InterferenceGraph InterferenceGraph::build(const ControlFlowGraph& cfg, const LivenessAnalysis& liveness) {
    InterferenceGraph graph;

    // Collect and register all nodes present in blocks/phis/instructions
    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;
        for (const auto& phi : bb_ptr->phis) {
            graph.add_node(phi.dest);
            for (const auto& [_, op] : phi.incoming) {
                if (op.is_vreg()) {
                    graph.add_node(op.as_vreg());
                }
            }
        }
        for (const auto& inst : bb_ptr->instructions()) {
            if (inst.dest.has_value()) {
                graph.add_node(*inst.dest);
            }
            if (inst.src1.has_value() && inst.src1->is_vreg()) {
                graph.add_node(inst.src1->as_vreg());
            }
            if (inst.src2.has_value() && inst.src2->is_vreg()) {
                graph.add_node(inst.src2->as_vreg());
            }
        }
    }

    for (const auto& bb_ptr : cfg.blocks()) {
        if (!bb_ptr) continue;
        BlockID bid = bb_ptr->id();

        // 1. Initialize live = liveness.get_live_out(B)
        std::unordered_set<VReg> live = liveness.get_live_out(bid);
        for (const auto& v : live) {
            graph.add_node(v);
        }

        // 2. Traverse instructions in B in reverse order
        const auto& insts = bb_ptr->instructions();
        for (auto it = insts.rbegin(); it != insts.rend(); ++it) {
            const auto& inst = *it;

            if (inst.opcode == Opcode::COPY && inst.dest.has_value() && inst.src1.has_value() && inst.src1->is_vreg()) {
                VReg dst = *inst.dest;
                VReg src = inst.src1->as_vreg();

                graph.add_node(dst);
                graph.add_node(src);

                // Record {dst, src} in move_list
                graph.move_list_.push_back(CopyEdge{dst, src});

                // Chaitin-Briggs rule: a copy dst = src does not cause dst and src to interfere
                for (const auto& v : live) {
                    if (v != src) {
                        graph.add_edge(dst, v);
                    }
                }

                // Remove dst from live, then add src to live
                live.erase(dst);
                live.insert(src);
            } else {
                // Standard non-copy instruction defining dst (or copy from immediate/label)
                if (inst.dest.has_value()) {
                    VReg dst = *inst.dest;
                    graph.add_node(dst);
                    for (const auto& v : live) {
                        graph.add_edge(dst, v);
                    }
                    live.erase(dst);
                }

                // Add all used VReg operands into live
                if (inst.src1.has_value() && inst.src1->is_vreg()) {
                    VReg s1 = inst.src1->as_vreg();
                    graph.add_node(s1);
                    live.insert(s1);
                }
                if (inst.src2.has_value() && inst.src2->is_vreg()) {
                    VReg s2 = inst.src2->as_vreg();
                    graph.add_node(s2);
                    live.insert(s2);
                }
            }
        }

        // If the block contains phi nodes, the phi definitions interfere with each other
        // and with any variables live into the block except themselves
        if (!bb_ptr->phis.empty()) {
            for (const auto& phi : bb_ptr->phis) {
                graph.add_node(phi.dest);
                for (const auto& v : live) {
                    graph.add_edge(phi.dest, v);
                }
            }
            for (std::size_t i = 0; i < bb_ptr->phis.size(); ++i) {
                for (std::size_t j = i + 1; j < bb_ptr->phis.size(); ++j) {
                    graph.add_edge(bb_ptr->phis[i].dest, bb_ptr->phis[j].dest);
                }
            }
        }
    }

    return graph;
}

InterferenceGraph build_interference_graph(const ControlFlowGraph& cfg, const LivenessAnalysis& liveness) {
    return InterferenceGraph::build(cfg, liveness);
}

} // namespace phinale
