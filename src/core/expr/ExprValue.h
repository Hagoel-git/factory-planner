#ifndef EXPRVALUE_H
#define EXPRVALUE_H

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace expr {

enum class ValueKind {
    Number,
    Bool,
    String,
    Option,
    Resource,
    Slots,
    NumberList
};

struct ExprType {
    ValueKind kind = ValueKind::Number;
    std::string option_set; // Non-empty for Option<S> and Slots<S>

    bool operator==(const ExprType& other) const {
        if (kind != other.kind) {
            return false;
        }
        if (kind == ValueKind::Option || kind == ValueKind::Slots) {
            return option_set == other.option_set;
        }
        return true;
    }

    bool operator!=(const ExprType& other) const {
        return !(*this == other);
    }

    static ExprType number() { return {ValueKind::Number, {}}; }
    static ExprType boolean() { return {ValueKind::Bool, {}}; }
    static ExprType string() { return {ValueKind::String, {}}; }
    static ExprType option(std::string set_id) { return {ValueKind::Option, std::move(set_id)}; }
    static ExprType resource() { return {ValueKind::Resource, {}}; }
    static ExprType slots(std::string set_id) { return {ValueKind::Slots, std::move(set_id)}; }
    static ExprType numberList() { return {ValueKind::NumberList, {}}; }

    std::string toString() const {
        switch (kind) {
            case ValueKind::Number:
                return "number";
            case ValueKind::Bool:
                return "bool";
            case ValueKind::String:
                return "string";
            case ValueKind::Option:
                return "option<" + option_set + ">";
            case ValueKind::Resource:
                return "resource";
            case ValueKind::Slots:
                return "slots<" + option_set + ">";
            case ValueKind::NumberList:
                return "list<number>";
        }
        return "unknown";
    }
};

enum class ExpectedType {
    Number,
    Bool,
    ResourceOrString,
    Any
};

inline bool matchesExpectedType(const ExprType& actual, ExpectedType expected) {
    switch (expected) {
        case ExpectedType::Number:
            return actual.kind == ValueKind::Number;
        case ExpectedType::Bool:
            return actual.kind == ValueKind::Bool;
        case ExpectedType::ResourceOrString:
            return actual.kind == ValueKind::Resource || actual.kind == ValueKind::String;
        case ExpectedType::Any:
            return true;
    }
    return false;
}

inline std::string expectedTypeDescription(ExpectedType expected) {
    switch (expected) {
        case ExpectedType::Number:
            return "number";
        case ExpectedType::Bool:
            return "bool";
        case ExpectedType::ResourceOrString:
            return "resource or string";
        case ExpectedType::Any:
            return "any";
    }
    return "unknown";
}

struct OptionValue {
    std::string option_set;
    std::optional<std::string> key; // std::nullopt when allow_none parameter/global is unset
};

struct ResourceValue {
    std::optional<std::string> id; // std::nullopt when allow_none parameter/global is unset
};

struct SlotsValue {
    std::string option_set;
    std::vector<std::optional<std::string>> active_slots; // Active slots in 0 .. count - 1
};

struct NumberListValue {
    std::vector<double> values; // Non-empty active slot values
};

using EvalValueVariant = std::variant<
    double,
    bool,
    std::string,
    OptionValue,
    ResourceValue,
    SlotsValue,
    NumberListValue>;

struct EvalValue {
    EvalValueVariant data = 0.0;

    static EvalValue number(double v) {
        if (v == 0.0) {
            v = 0.0; // Normalize -0.0 to +0.0
        }
        return EvalValue{v};
    }

    static EvalValue boolean(bool v) { return EvalValue{v}; }
    static EvalValue string(std::string v) { return EvalValue{std::move(v)}; }
    static EvalValue option(std::string set_id, std::optional<std::string> key) {
        return EvalValue{OptionValue{std::move(set_id), std::move(key)}};
    }
    static EvalValue resource(std::optional<std::string> id) {
        return EvalValue{ResourceValue{std::move(id)}};
    }
    static EvalValue slots(std::string set_id, std::vector<std::optional<std::string>> active) {
        return EvalValue{SlotsValue{std::move(set_id), std::move(active)}};
    }
    static EvalValue numberList(std::vector<double> vals) {
        return EvalValue{NumberListValue{std::move(vals)}};
    }

    bool isNumber() const { return std::holds_alternative<double>(data); }
    bool isBool() const { return std::holds_alternative<bool>(data); }
    bool isString() const { return std::holds_alternative<std::string>(data); }
    bool isOption() const { return std::holds_alternative<OptionValue>(data); }
    bool isResource() const { return std::holds_alternative<ResourceValue>(data); }
    bool isSlots() const { return std::holds_alternative<SlotsValue>(data); }
    bool isNumberList() const { return std::holds_alternative<NumberListValue>(data); }

    double asNumber() const { return std::get<double>(data); }
    bool asBool() const { return std::get<bool>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }
    const OptionValue& asOption() const { return std::get<OptionValue>(data); }
    const ResourceValue& asResource() const { return std::get<ResourceValue>(data); }
    const SlotsValue& asSlots() const { return std::get<SlotsValue>(data); }
    const NumberListValue& asNumberList() const { return std::get<NumberListValue>(data); }
};

struct EvalResult {
    bool ok = true;
    EvalValue value;
    std::string error;

    static EvalResult success(EvalValue val) {
        return {true, std::move(val), {}};
    }

    static EvalResult failure(std::string msg) {
        return {false, {}, std::move(msg)};
    }
};

inline double normalizeZero(double v) {
    return (v == 0.0) ? 0.0 : v;
}

inline constexpr std::array<std::string_view, 11> kReservedRootIdentifiers = {
    "game", "global", "machine", "recipe", "param",
    "port", "node", "option", "craft_time", "true", "false"
};

inline constexpr std::array<std::string_view, 18> kBuiltinFunctionNames = {
    "min", "max", "clamp", "floor", "ceil", "round",
    "abs", "sqrt", "pow", "exp", "ln", "log",
    "if", "lookup", "has_tag", "sum", "count", "is_set"
};

inline bool isReservedRootIdentifier(std::string_view name) {
    return std::find(kReservedRootIdentifiers.begin(), kReservedRootIdentifiers.end(), name) !=
           kReservedRootIdentifiers.end();
}

inline bool isBuiltinFunctionName(std::string_view name) {
    return std::find(kBuiltinFunctionNames.begin(), kBuiltinFunctionNames.end(), name) !=
           kBuiltinFunctionNames.end();
}

inline bool isReservedIdentifier(std::string_view name) {
    return isReservedRootIdentifier(name) || isBuiltinFunctionName(name);
}

// V-ID-1: entity ids, option keys, categories, tags match [a-z0-9_-]+
inline bool isValidSlugId(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (unsigned char c : s) {
        bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!valid) {
            return false;
        }
    }
    return true;
}

// V-ID-1: parameters, globals, let names, and attribute names match [a-z_][a-z0-9_]*
inline bool isValidIdentifierId(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    unsigned char first = static_cast<unsigned char>(s[0]);
    if (!((first >= 'a' && first <= 'z') || first == '_')) {
        return false;
    }
    for (size_t i = 1; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!valid) {
            return false;
        }
    }
    return true;
}

} // namespace expr

#endif // EXPRVALUE_H
