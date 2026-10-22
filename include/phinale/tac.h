#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace phinale {

using BlockID = std::size_t;
using VRegID = std::size_t;

struct VReg {
    std::size_t id{0};
    std::optional<std::size_t> version{std::nullopt};
    std::optional<std::size_t> original_id{std::nullopt};

    bool operator==(const VReg& other) const noexcept {
        if (version.has_value() && other.version.has_value()) {
            return id == other.id && version == other.version;
        }
        return id == other.id;
    }

    bool operator!=(const VReg& other) const noexcept {
        return !(*this == other);
    }

    bool operator<(const VReg& other) const noexcept {
        if (id != other.id) return id < other.id;
        return version < other.version;
    }
};

enum class OperandKind {
    VReg,
    IntConst,
    BoolConst,
    Label
};

struct Operand {
    using ValueType = std::variant<VReg, int64_t, bool, std::string>;
    ValueType value;

    static Operand from_vreg(VReg vreg) {
        return Operand{vreg};
    }

    static Operand from_int(int64_t val) {
        return Operand{val};
    }

    static Operand from_bool(bool val) {
        return Operand{val};
    }

    static Operand from_label(std::string label) {
        return Operand{std::move(label)};
    }

    bool is_vreg() const noexcept {
        return std::holds_alternative<VReg>(value);
    }

    bool is_int() const noexcept {
        return std::holds_alternative<int64_t>(value);
    }

    bool is_bool() const noexcept {
        return std::holds_alternative<bool>(value);
    }

    bool is_label() const noexcept {
        return std::holds_alternative<std::string>(value);
    }

    VReg as_vreg() const {
        return std::get<VReg>(value);
    }

    int64_t as_int() const {
        return std::get<int64_t>(value);
    }

    bool as_bool() const {
        return std::get<bool>(value);
    }

    const std::string& as_label() const {
        return std::get<std::string>(value);
    }

    bool operator==(const Operand& other) const noexcept {
        return value == other.value;
    }

    bool operator!=(const Operand& other) const noexcept {
        return !(*this == other);
    }
};

struct PhiNode {
    VReg dest;
    std::vector<std::pair<BlockID, Operand>> incoming;
    std::optional<VRegID> original_var{std::nullopt};

    void add_incoming(BlockID block, Operand op) {
        for (auto& entry : incoming) {
            if (entry.first == block) {
                entry.second = std::move(op);
                return;
            }
        }
        incoming.emplace_back(block, std::move(op));
    }

    std::optional<Operand> get_incoming(BlockID block) const {
        for (const auto& entry : incoming) {
            if (entry.first == block) {
                return entry.second;
            }
        }
        return std::nullopt;
    }

    bool operator==(const PhiNode& other) const noexcept {
        return dest == other.dest && incoming == other.incoming;
    }

    bool operator!=(const PhiNode& other) const noexcept {
        return !(*this == other);
    }
};

enum class Opcode {
    // Arithmetic & Bitwise
    ADD,
    SUB,
    MUL,
    DIV,
    NEG,

    // Relational & Logic
    CMP_EQ,
    CMP_NE,
    CMP_LT,
    CMP_LE,
    CMP_GT,
    CMP_GE,

    // Data Movement
    COPY,

    // Control Flow
    JUMP,
    JUMP_IF,

    // Functions
    PARAM,
    CALL,
    RETURN,

    // Pseudo
    LABEL
};

struct Instruction {
    Opcode opcode;
    std::optional<VReg> dest;
    std::optional<Operand> src1;
    std::optional<Operand> src2;
    std::optional<std::string> label_target;
    std::optional<std::string> false_label_target; // For conditional branching: JUMP_IF cond goto L_true else goto L_false
};

struct TACFunction {
    std::string name;
    std::vector<VReg> params;
    std::vector<Instruction> instructions;
};

struct TACProgram {
    std::vector<TACFunction> functions;
};

std::string to_string(const Operand& op);
std::string to_string(const PhiNode& phi);
std::string to_string(const Instruction& inst);
std::string to_string(const TACFunction& func);
std::string to_string(const TACProgram& prog);

} // namespace phinale

namespace std {
template <>
struct hash<phinale::VReg> {
    std::size_t operator()(const phinale::VReg& v) const noexcept {
        std::size_t h1 = std::hash<std::size_t>{}(v.id);
        if (v.version.has_value()) {
            std::size_t h2 = std::hash<std::size_t>{}(*v.version);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
        return h1;
    }
};
} // namespace std
