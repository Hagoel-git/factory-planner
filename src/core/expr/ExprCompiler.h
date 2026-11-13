#ifndef EXPRCOMPILER_H
#define EXPRCOMPILER_H

#include "core/expr/ExprAst.h"
#include "core/expr/ExprDiagnostic.h"
#include "core/expr/ExprEnv.h"
#include "core/expr/ExprValue.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace expr {

struct TokenizeResult {
    bool ok = true;
    std::vector<Token> tokens;
    CompileError error;
};

struct ParseResult {
    bool ok = true;
    std::unique_ptr<AstNode> root;
    CompileError error;
};

struct CompileResult {
    bool ok = true;
    CompiledExpr expr;
    CompileError error;
};

struct DependencyOrderResult {
    bool ok = true;
    // Topological order of cycle-free nodes (ties broken lexicographically by name).
    std::vector<std::string> order;
    // Nodes that lie on a cycle (SCC size > 1 or self-loop u -> u); these emit V-EXPR-6.
    std::vector<std::string> cycled_nodes;
    // Nodes not on a cycle that transitively depend on cycled_nodes (cascade-dropped per §9.1).
    std::vector<std::string> downstream_of_cycle;
    // Canonical cycle path [a, b, ..., a] starting at the lexicographically smallest cycled node.
    std::vector<std::string> canonical_cycle;
    // Formatted cycle string, e.g. "cycle oc -> t -> oc".
    std::string cycle_message;
};

struct LetCompileResult {
    bool ok = true;
    std::vector<std::string> evaluation_order;
    std::unordered_map<std::string, CompiledExpr> compiled_lets;
    std::unordered_map<std::string, ExprType> let_types;
    std::vector<CompileError> errors;
    DependencyOrderResult dep_order;
};

// Tokenizes an expression string per §5.1 lexical rules.
TokenizeResult tokenize(std::string_view source);

// Parses an expression string into an untyped AST per §5.2 EBNF grammar.
ParseResult parse(std::string_view source);

// Parses, type-checks, and context-checks an expression string per §5.2–§5.7.
CompileResult compile(
    std::string_view source,
    const CompileEnv& env,
    ExpectedType expected_type = ExpectedType::Number);

// Computes topological evaluation order (ties broken by name) and detects cycles (V-EXPR-6).
// deps_by_node[u] lists the nodes v that u depends on (v must be evaluated before u).
DependencyOrderResult resolveDependencyOrder(
    const std::unordered_map<std::string, std::vector<std::string>>& deps_by_node);

// Compiles a profile's `let` map (with optional recipe `overrides.let` and base `let_types`
// for V-OVR-1 / V-OVR-2 validation) in topological dependency order.
LetCompileResult compileLetMap(
    const std::unordered_map<std::string, std::string>& base_lets,
    const CompileEnv& base_env,
    const std::unordered_map<std::string, std::string>& override_lets = {},
    const std::unordered_map<std::string, ExprType>* required_base_types = nullptr);

} // namespace expr

#endif // EXPRCOMPILER_H
