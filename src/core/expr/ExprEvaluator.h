#ifndef EXPREVALUATOR_H
#define EXPREVALUATOR_H

#include "core/expr/ExprAst.h"
#include "core/expr/ExprEnv.h"
#include "core/expr/ExprValue.h"

namespace expr {

// Evaluates a compiled expression against the provided runtime environment (§5.7–§5.8).
// Never throws exceptions; runtime errors are returned via EvalResult::ok == false.
EvalResult evaluate(const CompiledExpr& expr, const EvalEnv& env);

} // namespace expr

#endif // EXPREVALUATOR_H
