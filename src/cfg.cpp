#include "phinale/cfg.hpp"

#include <sstream>

namespace phinale {

static std::string escape_dot(const std::string& str) {
    std::string res;
    for (char c : str) {
        if (c == '"' || c == '\\') {
            res += '\\';
            res += c;
        } else if (c == '\n') {
            res += "\\l";
        } else if (c == '<' || c == '>' || c == '{' || c == '}' || c == '|') {
            res += '\\';
            res += c;
        } else {
            res += c;
        }
    }
    return res;
}

void ControlFlowGraph::dump_dot(std::ostream& os) const {
    os << "digraph CFG {\n";
    os << "  node [shape=record, fontname=\"Courier\"];\n";

    for (const auto& bb : blocks_) {
        std::string label_str;
        if (bb->label().has_value()) {
            label_str = " [" + *bb->label() + "]";
        }

        std::ostringstream insts_ss;
        for (const auto& inst : bb->instructions()) {
            insts_ss << to_string(inst) << "\n";
        }

        std::string block_content = "Block " + std::to_string(bb->id()) + label_str + "\\n\\l" + escape_dot(insts_ss.str());

        os << "  bb" << bb->id() << " [label=\"" << block_content << "\"];\n";
    }

    os << "\n";

    for (const auto& bb : blocks_) {
        if (bb->instructions().empty()) {
            for (auto* succ : bb->successors()) {
                os << "  bb" << bb->id() << " -> bb" << succ->id() << ";\n";
            }
            continue;
        }

        const auto& last_inst = bb->instructions().back();
        if (last_inst.opcode == Opcode::JUMP_IF) {
            // Usually has two successors: first is true branch, second is false/fallthrough
            if (bb->successors().size() == 2) {
                os << "  bb" << bb->id() << " -> bb" << bb->successors()[0]->id() << " [label=\"true\", color=\"green\"];\n";
                os << "  bb" << bb->id() << " -> bb" << bb->successors()[1]->id() << " [label=\"false\", color=\"red\"];\n";
            } else {
                for (auto* succ : bb->successors()) {
                    os << "  bb" << bb->id() << " -> bb" << succ->id() << ";\n";
                }
            }
        } else {
            for (auto* succ : bb->successors()) {
                os << "  bb" << bb->id() << " -> bb" << succ->id() << ";\n";
            }
        }
    }

    os << "}\n";
}

} // namespace phinale
