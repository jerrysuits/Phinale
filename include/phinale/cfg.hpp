#pragma once

#include "phinale/tac.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace phinale {

struct PhiList : public std::vector<PhiNode> {
    using std::vector<PhiNode>::vector;
    PhiList& operator()() noexcept { return *this; }
    const PhiList& operator()() const noexcept { return *this; }
};

class BasicBlock {
public:
    explicit BasicBlock(BlockID id, std::optional<std::string> label = std::nullopt)
        : id_(id), label_(std::move(label)) {}

    BlockID id() const noexcept { return id_; }
    const std::optional<std::string>& label() const noexcept { return label_; }
    void set_label(std::string label) { label_ = std::move(label); }

    PhiList phis;

    void add_phi(PhiNode phi) {
        phis.push_back(std::move(phi));
    }

    bool has_phi() const noexcept {
        return !phis.empty();
    }

    std::vector<Instruction>& instructions() noexcept { return instructions_; }
    const std::vector<Instruction>& instructions() const noexcept { return instructions_; }

    std::vector<BasicBlock*>& successors() noexcept { return successors_; }
    const std::vector<BasicBlock*>& successors() const noexcept { return successors_; }

    std::vector<BasicBlock*>& predecessors() noexcept { return predecessors_; }
    const std::vector<BasicBlock*>& predecessors() const noexcept { return predecessors_; }

    void add_successor(BasicBlock* succ) {
        successors_.push_back(succ);
    }

    void add_predecessor(BasicBlock* pred) {
        predecessors_.push_back(pred);
    }

    void remove_successor(BasicBlock* succ) {
        successors_.erase(std::remove(successors_.begin(), successors_.end(), succ), successors_.end());
    }

    void remove_predecessor(BasicBlock* pred) {
        predecessors_.erase(std::remove(predecessors_.begin(), predecessors_.end(), pred), predecessors_.end());
    }

    void replace_successor(BasicBlock* old_succ, BasicBlock* new_succ) {
        for (auto& s : successors_) {
            if (s == old_succ) s = new_succ;
        }
    }

    void replace_predecessor(BasicBlock* old_pred, BasicBlock* new_pred) {
        for (auto& p : predecessors_) {
            if (p == old_pred) p = new_pred;
        }
    }

    bool is_terminated() const noexcept {
        if (instructions_.empty()) return false;
        Opcode op = instructions_.back().opcode;
        return op == Opcode::JUMP || op == Opcode::JUMP_IF || op == Opcode::RETURN;
    }

private:
    BlockID id_{0};
    std::optional<std::string> label_{std::nullopt};
    std::vector<Instruction> instructions_;
    std::vector<BasicBlock*> successors_;
    std::vector<BasicBlock*> predecessors_;
};

class ControlFlowGraph {
public:
    ControlFlowGraph() = default;

    BasicBlock* create_block(std::optional<std::string> label = std::nullopt) {
        BlockID id = blocks_.size();
        auto block = std::make_unique<BasicBlock>(id, std::move(label));
        BasicBlock* ptr = block.get();
        blocks_.push_back(std::move(block));
        return ptr;
    }

    std::size_t size() const noexcept {
        return blocks_.size();
    }

    const std::vector<std::unique_ptr<BasicBlock>>& blocks() const noexcept {
        return blocks_;
    }

    BasicBlock* get_block(BlockID id) const {
        if (id < blocks_.size()) {
            return blocks_[id].get();
        }
        return nullptr;
    }

    BasicBlock* entry() const noexcept { return entry_; }
    void set_entry(BasicBlock* entry) noexcept { entry_ = entry; }

    const std::vector<BasicBlock*>& exits() const noexcept { return exits_; }
    void add_exit(BasicBlock* exit) { exits_.push_back(exit); }

    std::vector<BasicBlock*> reverse_post_order() const {
        std::vector<BasicBlock*> rpo;
        if (!entry_) return rpo;

        std::unordered_set<BasicBlock*> visited;
        std::vector<BasicBlock*> post_order;

        auto dfs = [&](auto& self, BasicBlock* bb) -> void {
            visited.insert(bb);
            for (auto* succ : bb->successors()) {
                if (succ && visited.find(succ) == visited.end()) {
                    self(self, succ);
                }
            }
            post_order.push_back(bb);
        };

        dfs(dfs, entry_);
        rpo.assign(post_order.rbegin(), post_order.rend());
        return rpo;
    }

    void dump_dot(std::ostream& os) const;

private:
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
    BasicBlock* entry_{nullptr};
    std::vector<BasicBlock*> exits_;
};

} // namespace phinale
