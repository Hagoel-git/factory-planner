#ifndef EXPRAST_H
#define EXPRAST_H

#include "core/expr/ExprValue.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace expr {

enum class TokenKind {
    Number,
    String,
    Ident,
    True,
    False,
    Or,        // ||
    And,       // &&
    Not,       // !
    Eq,        // ==
    Ne,        // !=
    Lt,        // <
    Le,        // <=
    Gt,        // >
    Ge,        // >=
    Plus,      // +
    Minus,     // -
    Star,      // *
    Slash,     // /
    Percent,   // %
    Caret,     // ^
    Dot,       // .
    LParen,    // (
    RParen,    // )
    Comma,     // ,
    EndOfFile
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string text;
    double number_value = 0.0;
    size_t line = 1;
    size_t column = 1;
    size_t offset = 0;
};

enum class UnaryOpKind {
    Negate, // -x
    Not     // !x
};

enum class BinaryOpKind {
    Or,  // ||
    And, // &&
    Eq,  // ==
    Ne,  // !=
    Lt,  // <
    Le,  // <=
    Gt,  // >
    Ge,  // >=
    Add, // +
    Sub, // -
    Mul, // *
    Div, // /
    Mod, // %
    Pow  // ^
};

enum class AstKind {
    NumberLit,
    BoolLit,
    StringLit,
    Ident,
    Member,
    UnaryOp,
    BinaryOp,
    Call
};

enum class ResolvedAccessKind {
    None,
    // Bare identifiers:
    LetVar,
    CraftTime,
    OptionCandidate,
    // Namespace root accesses (root.field):
    GameTicksPerSecond,
    GameAttr,
    GlobalAccess,
    MachineId,
    MachineSpeed,
    MachineAttr,
    RecipeId,
    RecipeTime,
    RecipeTimeRaw,
    RecipeInputItemsTotal,
    RecipeInputFluidsTotal,
    RecipeOutputItemsTotal,
    RecipeOutputFluidsTotal,
    RecipeAttr,
    ParamAccess,
    PortDir,
    PortAmount,
    PortChance,
    PortCatalyst,
    PortIndex,
    PortBaseResource,
    PortResource,
    PortAttr,
    NodeMachines,
    NodeCrafts,
    // Value postfix member accesses (expr.field):
    OptionKey,
    OptionAttr,
    ResourceId,
    ResourceAttr,
    SlotsAttr
};

enum class BuiltinFunc {
    MinScalar,
    MinList,
    MaxScalar,
    MaxList,
    Clamp,
    Floor,
    Ceil,
    Round,
    Abs,
    Sqrt,
    Pow,
    Exp,
    Ln,
    Log,
    If,
    LookupArray,
    LookupObject,
    HasTag,
    Sum,
    CountAll,
    CountKey,
    IsSetParam,
    IsSetGlobal
};

struct AstNode {
    AstKind kind = AstKind::NumberLit;
    size_t line = 1;
    size_t column = 1;

    // Literal payloads
    double number_value = 0.0;
    bool bool_value = false;
    std::string string_value;

    // Identifier / Member / Call name
    std::string name;

    // Unary / Binary operators
    UnaryOpKind unary_op = UnaryOpKind::Negate;
    BinaryOpKind binary_op = BinaryOpKind::Add;

    // Child nodes
    std::unique_ptr<AstNode> left;   // Also used as unary operand or Member object
    std::unique_ptr<AstNode> right;
    std::vector<std::unique_ptr<AstNode>> args;

    // Position of member field identifier (after '.')
    size_t field_line = 1;
    size_t field_column = 1;

    // Type-checker annotations
    ExprType resolved_type;
    ResolvedAccessKind access_kind = ResolvedAccessKind::None;
    std::string resolved_key; // Attribute name, param id, global id, or let name
    BuiltinFunc builtin_func = BuiltinFunc::MinScalar;
};

struct CompiledExpr {
    std::string source;
    std::unique_ptr<AstNode> root;
    ExprType type;

    // Dependency metadata populated during compilation:
    std::vector<std::string> referenced_lets;
    std::vector<std::string> referenced_globals;
    std::vector<std::string> referenced_params;
    std::vector<std::string> referenced_tables;
    std::vector<std::string> referenced_resources;
    std::vector<std::string> referenced_machine_attrs;
    std::vector<std::string> referenced_recipe_attrs;
    std::vector<std::string> referenced_port_attrs;
    std::vector<std::string> referenced_resource_attrs;
    bool references_port = false;
    bool references_port_resource = false;

    bool isValid() const { return root != nullptr; }
};

} // namespace expr

#endif // EXPRAST_H
