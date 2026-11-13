#include "core/expr/ExprCompiler.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace expr {

namespace {

void addUnique(std::vector<std::string>& vec, const std::string& item) {
    if (std::find(vec.begin(), vec.end(), item) == vec.end()) {
        vec.push_back(item);
    }
}

CompileError makeError(
    std::string rule_id,
    size_t line,
    size_t col,
    std::string msg,
    std::string_view source,
    OffendingSymbolKind kind = OffendingSymbolKind::None,
    std::string symbol = {},
    std::string parent = {}) {
    CompileError err;
    err.rule_id = std::move(rule_id);
    err.line = line;
    err.column = col;
    err.message = std::move(msg);
    err.expr_source = std::string(source);
    err.offending_kind = kind;
    err.offending_symbol = std::move(symbol);
    err.offending_parent = std::move(parent);
    return err;
}

std::string appendSuggestion(
    std::string base_msg,
    std::string_view target,
    std::span<const std::string> candidates,
    SuggestionBudget* budget,
    std::string_view prefix = {}) {
    auto sug = findBestSuggestion(target, candidates, budget);
    if (sug.has_value()) {
        base_msg += " (did you mean '";
        base_msg += prefix;
        base_msg += *sug;
        base_msg += "'?)";
    }
    return base_msg;
}

template <typename MapType>
std::vector<std::string> mapKeys(const MapType& m) {
    std::vector<std::string> keys;
    keys.reserve(m.size());
    for (const auto& kv : m) {
        keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::string> setKeys(const std::unordered_set<std::string>& s) {
    std::vector<std::string> keys(s.begin(), s.end());
    std::sort(keys.begin(), keys.end());
    return keys;
}

} // namespace

// ============================================================================
// 1. Lexer (§5.1)
// ============================================================================

TokenizeResult tokenize(std::string_view source) {
    TokenizeResult res;
    size_t i = 0;
    size_t line = 1;
    size_t col = 1;

    auto advance = [&](size_t count = 1) {
        for (size_t k = 0; k < count && i < source.size(); ++k) {
            if (source[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
            ++i;
        }
    };

    while (i < source.size()) {
        unsigned char c = static_cast<unsigned char>(source[i]);

        // Skip whitespace
        if (std::isspace(c)) {
            advance();
            continue;
        }

        const size_t tok_line = line;
        const size_t tok_col = col;
        const size_t tok_offset = i;

        // Number literal: [0-9]+ ( '.' [0-9]+ )? ( [eE] [+-]? [0-9]+ )?
        // No leading '.', no hex, no suffixes.
        if (std::isdigit(c)) {
            size_t start = i;
            while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
                advance();
            }
            if (i < source.size() && source[i] == '.') {
                advance(); // consume '.'
                if (i >= source.size() || !std::isdigit(static_cast<unsigned char>(source[i]))) {
                    res.ok = false;
                    res.error = makeError(
                        "V-EXPR-1", tok_line, col,
                        "invalid number literal: expected digit after '.'", source);
                    return res;
                }
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
                    advance();
                }
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                advance(); // consume 'e'/'E'
                if (i < source.size() && (source[i] == '+' || source[i] == '-')) {
                    advance();
                }
                if (i >= source.size() || !std::isdigit(static_cast<unsigned char>(source[i]))) {
                    res.ok = false;
                    res.error = makeError(
                        "V-EXPR-1", tok_line, col,
                        "invalid number literal: expected exponent digits", source);
                    return res;
                }
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) {
                    advance();
                }
            }
            // Reject trailing identifier characters (e.g. 0x10, 1.5f) or extra decimal points (1.2.3)
            if (i < source.size()) {
                unsigned char next_c = static_cast<unsigned char>(source[i]);
                if (std::isalpha(next_c) || next_c == '_' || next_c == '.') {
                    res.ok = false;
                    res.error = makeError(
                        "V-EXPR-1", tok_line, col,
                        "invalid number literal '" +
                            std::string(source.substr(start, i - start + 1)) + "'",
                        source);
                    return res;
                }
            }

            std::string_view num_str = source.substr(start, i - start);
            double val = 0.0;
            auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), val);
            if (ec != std::errc() || ptr != num_str.data() + num_str.size() || !std::isfinite(val)) {
                res.ok = false;
                res.error = makeError(
                    "V-EXPR-1", tok_line, tok_col,
                    "number literal out of range or invalid: '" + std::string(num_str) + "'",
                    source);
                return res;
            }
            res.tokens.push_back(Token{
                TokenKind::Number,
                std::string(num_str),
                normalizeZero(val),
                tok_line,
                tok_col,
                tok_offset
            });
            continue;
        }

        // String literal: "..." or '...' with only \", \', and \\ escapes
        if (c == '"' || c == '\'') {
            char quote = static_cast<char>(c);
            advance(); // consume opening quote
            std::string value;
            bool closed = false;
            while (i < source.size()) {
                char ch = source[i];
                if (ch == quote) {
                    advance();
                    closed = true;
                    break;
                }
                if (ch == '\n' || ch == '\r') {
                    break; // Unterminated string before newline
                }
                if (ch == '\\') {
                    size_t esc_line = line;
                    size_t esc_col = col;
                    advance(); // consume '\'
                    if (i >= source.size()) {
                        res.ok = false;
                        res.error = makeError(
                            "V-EXPR-1", esc_line, esc_col,
                            "unterminated string literal after '\\'", source);
                        return res;
                    }
                    char esc = source[i];
                    if (esc == '"' || esc == '\'' || esc == '\\') {
                        value.push_back(esc);
                        advance();
                    } else {
                        res.ok = false;
                        res.error = makeError(
                            "V-EXPR-1", esc_line, esc_col,
                            std::string("invalid escape sequence '\\") + esc +
                                "' (only \\\", \\', and \\\\ are allowed)",
                            source);
                        return res;
                    }
                } else {
                    value.push_back(ch);
                    advance();
                }
            }
            if (!closed) {
                res.ok = false;
                res.error = makeError(
                    "V-EXPR-1", tok_line, tok_col, "unterminated string literal", source);
                return res;
            }
            res.tokens.push_back(Token{
                TokenKind::String,
                std::move(value),
                0.0,
                tok_line,
                tok_col,
                tok_offset
            });
            continue;
        }

        // Identifier or keyword: [A-Za-z_][A-Za-z0-9_]*
        if (std::isalpha(c) || c == '_') {
            size_t start = i;
            while (i < source.size()) {
                unsigned char nc = static_cast<unsigned char>(source[i]);
                if (std::isalnum(nc) || nc == '_') {
                    advance();
                } else {
                    break;
                }
            }
            std::string ident(source.substr(start, i - start));
            TokenKind kind = TokenKind::Ident;
            if (ident == "true") {
                kind = TokenKind::True;
            } else if (ident == "false") {
                kind = TokenKind::False;
            }
            res.tokens.push_back(Token{
                kind,
                std::move(ident),
                0.0,
                tok_line,
                tok_col,
                tok_offset
            });
            continue;
        }

        // Two-character operators
        if (i + 1 < source.size()) {
            std::string_view two = source.substr(i, 2);
            if (two == "||") {
                advance(2);
                res.tokens.push_back({TokenKind::Or, "||", 0.0, tok_line, tok_col, tok_offset});
                continue;
            }
            if (two == "&&") {
                advance(2);
                res.tokens.push_back({TokenKind::And, "&&", 0.0, tok_line, tok_col, tok_offset});
                continue;
            }
            if (two == "==") {
                advance(2);
                res.tokens.push_back({TokenKind::Eq, "==", 0.0, tok_line, tok_col, tok_offset});
                continue;
            }
            if (two == "!=") {
                advance(2);
                res.tokens.push_back({TokenKind::Ne, "!=", 0.0, tok_line, tok_col, tok_offset});
                continue;
            }
            if (two == "<=") {
                advance(2);
                res.tokens.push_back({TokenKind::Le, "<=", 0.0, tok_line, tok_col, tok_offset});
                continue;
            }
            if (two == ">=") {
                advance(2);
                res.tokens.push_back({TokenKind::Ge, ">=", 0.0, tok_line, tok_col, tok_offset});
                continue;
            }
        }

        // Single-character operators and punctuation
        switch (source[i]) {
            case '!':
                advance();
                res.tokens.push_back({TokenKind::Not, "!", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '<':
                advance();
                res.tokens.push_back({TokenKind::Lt, "<", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '>':
                advance();
                res.tokens.push_back({TokenKind::Gt, ">", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '+':
                advance();
                res.tokens.push_back({TokenKind::Plus, "+", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '-':
                advance();
                res.tokens.push_back({TokenKind::Minus, "-", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '*':
                advance();
                res.tokens.push_back({TokenKind::Star, "*", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '/':
                advance();
                res.tokens.push_back({TokenKind::Slash, "/", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '%':
                advance();
                res.tokens.push_back({TokenKind::Percent, "%", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '^':
                advance();
                res.tokens.push_back({TokenKind::Caret, "^", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '.':
                advance();
                res.tokens.push_back({TokenKind::Dot, ".", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case '(':
                advance();
                res.tokens.push_back({TokenKind::LParen, "(", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case ')':
                advance();
                res.tokens.push_back({TokenKind::RParen, ")", 0.0, tok_line, tok_col, tok_offset});
                continue;
            case ',':
                advance();
                res.tokens.push_back({TokenKind::Comma, ",", 0.0, tok_line, tok_col, tok_offset});
                continue;
            default:
                res.ok = false;
                res.error = makeError(
                    "V-EXPR-1", tok_line, tok_col,
                    std::string("unexpected character '") + source[i] + "'", source);
                return res;
        }
    }

    res.tokens.push_back(Token{TokenKind::EndOfFile, "", 0.0, line, col, i});
    return res;
}

// ============================================================================
// 2. Recursive-Descent Parser (§5.2)
// ============================================================================

namespace {

class Parser {
public:
    Parser(std::string_view source, std::vector<Token> tokens)
        : m_source(source), m_tokens(std::move(tokens)) {}

    ParseResult parseExprFull() {
        ParseResult res;
        auto root = parseOr();
        if (!m_ok) {
            res.ok = false;
            res.error = std::move(m_error);
            return res;
        }
        if (peek().kind != TokenKind::EndOfFile) {
            fail(peek(), "unexpected token '" + tokenDescription(peek()) + "'");
            res.ok = false;
            res.error = std::move(m_error);
            return res;
        }
        res.ok = true;
        res.root = std::move(root);
        return res;
    }

private:
    const Token& peek() const { return m_tokens[m_pos]; }

    const Token& advance() {
        const Token& tok = m_tokens[m_pos];
        if (m_pos + 1 < m_tokens.size()) {
            ++m_pos;
        }
        return tok;
    }

    static std::string tokenDescription(const Token& tok) {
        if (tok.kind == TokenKind::EndOfFile) {
            return "end of expression";
        }
        if (tok.kind == TokenKind::String) {
            return "\"" + tok.text + "\"";
        }
        return tok.text;
    }

    void fail(const Token& tok, std::string message) {
        if (!m_ok) {
            return;
        }
        m_ok = false;
        m_error = makeError("V-EXPR-1", tok.line, tok.column, std::move(message), m_source);
    }

    // or = and { "||" and } ;
    std::unique_ptr<AstNode> parseOr() {
        auto left = parseAnd();
        while (m_ok && peek().kind == TokenKind::Or) {
            Token op_tok = advance();
            auto right = parseAnd();
            if (!m_ok) {
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::BinaryOp;
            node->binary_op = BinaryOpKind::Or;
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    // and = not { "&&" not } ;
    std::unique_ptr<AstNode> parseAnd() {
        auto left = parseNot();
        while (m_ok && peek().kind == TokenKind::And) {
            Token op_tok = advance();
            auto right = parseNot();
            if (!m_ok) {
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::BinaryOp;
            node->binary_op = BinaryOpKind::And;
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    // not = "!" not | cmp ;
    std::unique_ptr<AstNode> parseNot() {
        if (peek().kind == TokenKind::Not) {
            Token op_tok = advance();
            auto operand = parseNot();
            if (!m_ok) {
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::UnaryOp;
            node->unary_op = UnaryOpKind::Not;
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(operand);
            return node;
        }
        return parseCmp();
    }

    static bool isComparisonToken(TokenKind k) {
        return k == TokenKind::Eq || k == TokenKind::Ne ||
               k == TokenKind::Lt || k == TokenKind::Le ||
               k == TokenKind::Gt || k == TokenKind::Ge;
    }

    static BinaryOpKind tokenToCmpOp(TokenKind k) {
        switch (k) {
            case TokenKind::Eq: return BinaryOpKind::Eq;
            case TokenKind::Ne: return BinaryOpKind::Ne;
            case TokenKind::Lt: return BinaryOpKind::Lt;
            case TokenKind::Le: return BinaryOpKind::Le;
            case TokenKind::Gt: return BinaryOpKind::Gt;
            case TokenKind::Ge: return BinaryOpKind::Ge;
            default: return BinaryOpKind::Eq;
        }
    }

    // cmp = sum [ ( "==" | "!=" | "<" | "<=" | ">" | ">=" ) sum ] ;
    std::unique_ptr<AstNode> parseCmp() {
        auto left = parseSum();
        if (!m_ok) {
            return nullptr;
        }
        if (isComparisonToken(peek().kind)) {
            Token op_tok = advance();
            auto right = parseSum();
            if (!m_ok) {
                return nullptr;
            }
            // Comparisons do not chain (§5.2: a < b < c is a syntax error)
            if (isComparisonToken(peek().kind)) {
                fail(peek(),
                     "comparisons cannot be chained (unexpected '" + peek().text + "')");
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::BinaryOp;
            node->binary_op = tokenToCmpOp(op_tok.kind);
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(left);
            node->right = std::move(right);
            return node;
        }
        return left;
    }

    // sum = term { ( "+" | "-" ) term } ;
    std::unique_ptr<AstNode> parseSum() {
        auto left = parseTerm();
        while (m_ok && (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus)) {
            Token op_tok = advance();
            auto right = parseTerm();
            if (!m_ok) {
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::BinaryOp;
            node->binary_op = (op_tok.kind == TokenKind::Plus) ? BinaryOpKind::Add : BinaryOpKind::Sub;
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    // term = unary { ( "*" | "/" | "%" ) unary } ;
    std::unique_ptr<AstNode> parseTerm() {
        auto left = parseUnary();
        while (m_ok && (peek().kind == TokenKind::Star ||
                        peek().kind == TokenKind::Slash ||
                        peek().kind == TokenKind::Percent)) {
            Token op_tok = advance();
            auto right = parseUnary();
            if (!m_ok) {
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::BinaryOp;
            if (op_tok.kind == TokenKind::Star) {
                node->binary_op = BinaryOpKind::Mul;
            } else if (op_tok.kind == TokenKind::Slash) {
                node->binary_op = BinaryOpKind::Div;
            } else {
                node->binary_op = BinaryOpKind::Mod;
            }
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(left);
            node->right = std::move(right);
            left = std::move(node);
        }
        return left;
    }

    // unary = "-" unary | power ;
    std::unique_ptr<AstNode> parseUnary() {
        if (peek().kind == TokenKind::Minus) {
            Token op_tok = advance();
            auto operand = parseUnary();
            if (!m_ok) {
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::UnaryOp;
            node->unary_op = UnaryOpKind::Negate;
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(operand);
            return node;
        }
        return parsePower();
    }

    // power = postfix [ "^" unary ] ;
    // Right-associative and binds tighter than unary minus: -2^2 = -4, 2^3^2 = 512, 2^-1 = 0.5
    std::unique_ptr<AstNode> parsePower() {
        auto base = parsePostfix();
        if (!m_ok) {
            return nullptr;
        }
        if (peek().kind == TokenKind::Caret) {
            Token op_tok = advance();
            auto exponent = parseUnary();
            if (!m_ok) {
                return nullptr;
            }
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::BinaryOp;
            node->binary_op = BinaryOpKind::Pow;
            node->line = op_tok.line;
            node->column = op_tok.column;
            node->left = std::move(base);
            node->right = std::move(exponent);
            return node;
        }
        return base;
    }

    // postfix = primary { "." IDENT } ;
    std::unique_ptr<AstNode> parsePostfix() {
        auto expr = parsePrimary();
        while (m_ok && peek().kind == TokenKind::Dot) {
            Token dot_tok = advance();
            if (peek().kind != TokenKind::Ident) {
                fail(peek(),
                     "expected identifier after '.' but found " + tokenDescription(peek()));
                return nullptr;
            }
            Token field_tok = advance();
            auto node = std::make_unique<AstNode>();
            node->kind = AstKind::Member;
            node->line = expr->line;
            node->column = expr->column;
            node->field_line = field_tok.line;
            node->field_column = field_tok.column;
            node->name = field_tok.text;
            node->left = std::move(expr);
            expr = std::move(node);
            (void)dot_tok;
        }
        return expr;
    }

    // primary = NUMBER | STRING | "true" | "false" | call | IDENT | "(" expr ")" ;
    // call    = IDENT "(" [ expr { "," expr } ] ")" ;
    std::unique_ptr<AstNode> parsePrimary() {
        const Token& tok = peek();
        switch (tok.kind) {
            case TokenKind::Number: {
                Token t = advance();
                auto node = std::make_unique<AstNode>();
                node->kind = AstKind::NumberLit;
                node->line = t.line;
                node->column = t.column;
                node->number_value = t.number_value;
                return node;
            }
            case TokenKind::String: {
                Token t = advance();
                auto node = std::make_unique<AstNode>();
                node->kind = AstKind::StringLit;
                node->line = t.line;
                node->column = t.column;
                node->string_value = std::move(t.text);
                return node;
            }
            case TokenKind::True:
            case TokenKind::False: {
                Token t = advance();
                auto node = std::make_unique<AstNode>();
                node->kind = AstKind::BoolLit;
                node->line = t.line;
                node->column = t.column;
                node->bool_value = (t.kind == TokenKind::True);
                return node;
            }
            case TokenKind::Ident: {
                Token id_tok = advance();
                if (peek().kind == TokenKind::LParen) {
                    advance(); // consume '('
                    auto call = std::make_unique<AstNode>();
                    call->kind = AstKind::Call;
                    call->line = id_tok.line;
                    call->column = id_tok.column;
                    call->name = std::move(id_tok.text);

                    if (peek().kind != TokenKind::RParen) {
                        while (true) {
                            auto arg = parseOr();
                            if (!m_ok) {
                                return nullptr;
                            }
                            call->args.push_back(std::move(arg));
                            if (peek().kind == TokenKind::Comma) {
                                advance();
                                continue;
                            }
                            break;
                        }
                    }
                    if (peek().kind != TokenKind::RParen) {
                        fail(peek(),
                             "expected ')' but found " + tokenDescription(peek()));
                        return nullptr;
                    }
                    advance(); // consume ')'
                    return call;
                }
                auto ident = std::make_unique<AstNode>();
                ident->kind = AstKind::Ident;
                ident->line = id_tok.line;
                ident->column = id_tok.column;
                ident->name = std::move(id_tok.text);
                return ident;
            }
            case TokenKind::LParen: {
                advance(); // consume '('
                auto inner = parseOr();
                if (!m_ok) {
                    return nullptr;
                }
                if (peek().kind != TokenKind::RParen) {
                    fail(peek(),
                         "expected ')' but found " + tokenDescription(peek()));
                    return nullptr;
                }
                advance(); // consume ')'
                return inner;
            }
            default:
                fail(tok, "expected expression but found " + tokenDescription(tok));
                return nullptr;
        }
    }

    std::string_view m_source;
    std::vector<Token> m_tokens;
    size_t m_pos = 0;
    bool m_ok = true;
    CompileError m_error;
};

} // namespace

ParseResult parse(std::string_view source) {
    TokenizeResult tok_res = tokenize(source);
    if (!tok_res.ok) {
        ParseResult res;
        res.ok = false;
        res.error = std::move(tok_res.error);
        return res;
    }
    Parser parser(source, std::move(tok_res.tokens));
    return parser.parseExprFull();
}

// ============================================================================
// 3. Type & Context Checker (§5.3–§5.7)
// ============================================================================

namespace {

class TypeChecker {
public:
    TypeChecker(std::string_view source, const CompileEnv& env, CompiledExpr& out_compiled)
        : m_source(source), m_env(env), m_compiled(out_compiled) {}

    bool checkRoot(AstNode& root, ExpectedType expected) {
        ExprType actual = checkNode(root);
        if (!m_ok) {
            return false;
        }

        if (!matchesExpectedType(actual, expected)) {
            fail("V-EXPR-4", root.line, root.column,
                 "result must be " + expectedTypeDescription(expected) +
                     ", got " + actual.toString());
            return false;
        }

        // §5.7 & R2-08: In resource_expr (ExpectedType::ResourceOrString), check non-empty (!= "")
        // string literals in result position (the root or the 2nd/3rd arguments of if() recursively)
        // against known resources.
        if (expected == ExpectedType::ResourceOrString && actual.kind == ValueKind::String) {
            if (!checkResourceExprResultLiterals(root)) {
                return false;
            }
        }

        m_compiled.type = actual;
        return true;
    }

    const CompileError& error() const { return m_error; }

private:
    void fail(
        std::string rule_id,
        size_t line,
        size_t col,
        std::string msg,
        OffendingSymbolKind kind = OffendingSymbolKind::None,
        std::string symbol = {},
        std::string parent = {}) {
        if (!m_ok) {
            return;
        }
        m_ok = false;
        m_error = makeError(
            std::move(rule_id), line, col, std::move(msg), m_source,
            kind, std::move(symbol), std::move(parent));
    }

    bool checkResourceExprResultLiterals(const AstNode& node) {
        if (node.kind == AstKind::StringLit) {
            if (!node.string_value.empty()) {
                if (m_env.resources.find(node.string_value) == m_env.resources.end()) {
                    auto candidates = setKeys(m_env.resources);
                    std::string msg = appendSuggestion(
                        "unknown resource '" + node.string_value + "'",
                        node.string_value, candidates, m_env.suggestion_budget);
                    fail("V-REF-1", node.line, node.column, std::move(msg),
                         OffendingSymbolKind::Resource, node.string_value);
                    return false;
                }
                addUnique(m_compiled.referenced_resources, node.string_value);
            }
            return true;
        }
        if (node.kind == AstKind::Call && node.builtin_func == BuiltinFunc::If && node.args.size() == 3) {
            return checkResourceExprResultLiterals(*node.args[1]) &&
                   checkResourceExprResultLiterals(*node.args[2]);
        }
        return true;
    }

    bool isContextStatic() const {
        return m_env.context == ExprContext::GlobalStatic ||
               m_env.context == ExprContext::ParamStatic;
    }

    bool isCraftTimeAllowed() const {
        return m_env.context == ExprContext::PreSolveAfterCraftTime ||
               m_env.context == ExprContext::RecipePortResourceExpr ||
               m_env.context == ExprContext::RecipePortFormula ||
               m_env.context == ExprContext::PostSolveMetricValue;
    }

    bool isPortAllowed() const {
        if (m_env.context == ExprContext::RecipePortResourceExpr ||
            m_env.context == ExprContext::RecipePortFormula) {
            return true;
        }
        if (m_env.context == ExprContext::ParamStatic && m_env.allow_port_in_static) {
            return true;
        }
        return false;
    }

    bool isPortResourceAllowed() const {
        return m_env.context == ExprContext::RecipePortFormula;
    }

    static bool isNamespaceRoot(std::string_view name) {
        return name == "game" || name == "global" || name == "machine" ||
               name == "recipe" || name == "param" || name == "port" || name == "node";
    }

    ExprType checkNode(AstNode& node) {
        if (!m_ok) {
            return ExprType::number();
        }
        switch (node.kind) {
            case AstKind::NumberLit:
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            case AstKind::BoolLit:
                node.resolved_type = ExprType::boolean();
                return node.resolved_type;
            case AstKind::StringLit:
                node.resolved_type = ExprType::string();
                return node.resolved_type;
            case AstKind::Ident:
                return checkBareIdent(node);
            case AstKind::Member:
                return checkMember(node);
            case AstKind::UnaryOp:
                return checkUnary(node);
            case AstKind::BinaryOp:
                return checkBinary(node);
            case AstKind::Call:
                return checkCall(node);
        }
        return ExprType::number();
    }

    ExprType checkBareIdent(AstNode& node) {
        const std::string& name = node.name;

        // Namespace roots cannot be used as standalone values without '.field'
        if (isNamespaceRoot(name)) {
            fail("V-EXPR-4", node.line, node.column,
                 "'" + name + "' must be followed by '.<field>'");
            return ExprType::number();
        }

        // 'option' candidate in allowed_if
        if (name == "option") {
            if (!m_env.allow_option) {
                fail("V-EXPR-3", node.line, node.column,
                     "'option' is only available in 'allowed_if' expressions");
                return ExprType::number();
            }
            node.access_kind = ResolvedAccessKind::OptionCandidate;
            node.resolved_type = m_env.option_type;
            return node.resolved_type;
        }

        // 'craft_time'
        if (name == "craft_time") {
            if (!isCraftTimeAllowed()) {
                fail("V-EXPR-3", node.line, node.column,
                     "'craft_time' is only available in port, extra-port, and metric formulas");
                return ExprType::number();
            }
            node.access_kind = ResolvedAccessKind::CraftTime;
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // Let variable
        auto let_it = m_env.let_types.find(name);
        if (let_it != m_env.let_types.end()) {
            if (isContextStatic()) {
                fail("V-EXPR-3", node.line, node.column,
                     "let variable '" + name + "' is not available in static expressions");
                return ExprType::number();
            }
            node.access_kind = ResolvedAccessKind::LetVar;
            node.resolved_key = name;
            node.resolved_type = let_it->second;
            addUnique(m_compiled.referenced_lets, name);
            return node.resolved_type;
        }

        // Unknown bare identifier: build candidate list for "did you mean"
        std::vector<std::string> candidates = {
            "game", "global", "machine", "recipe", "param", "port", "node", "true", "false"
        };
        if (m_env.allow_option) {
            candidates.push_back("option");
        }
        if (isCraftTimeAllowed()) {
            candidates.push_back("craft_time");
        }
        if (!isContextStatic()) {
            for (const auto& kv : m_env.let_types) {
                candidates.push_back(kv.first);
            }
        }
        std::sort(candidates.begin(), candidates.end());

        std::string msg = appendSuggestion(
            "unknown identifier '" + name + "'", name, candidates, m_env.suggestion_budget);
        fail("V-EXPR-2", node.line, node.column, std::move(msg),
             OffendingSymbolKind::Identifier, name);
        return ExprType::number();
    }

    ExprType checkNamespaceMember(AstNode& node, const std::string& root, const std::string& field) {
        // 1. game.<field>
        if (root == "game") {
            if (field == "ticks_per_second") {
                node.access_kind = ResolvedAccessKind::GameTicksPerSecond;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            auto it = m_env.game_attrs.find(field);
            if (it != m_env.game_attrs.end()) {
                node.access_kind = ResolvedAccessKind::GameAttr;
                node.resolved_key = field;
                node.resolved_type = attrTypeToExprType(it->second);
                return node.resolved_type;
            }
            std::vector<std::string> candidates = mapKeys(m_env.game_attrs);
            candidates.push_back("ticks_per_second");
            std::sort(candidates.begin(), candidates.end());
            std::string msg = appendSuggestion(
                "unknown identifier 'game." + field + "'",
                field, candidates, m_env.suggestion_budget, "game.");
            fail("V-EXPR-2", node.line, node.column, std::move(msg),
                 OffendingSymbolKind::Attribute, field, "game");
            return ExprType::number();
        }

        // 2. global.<id>
        if (root == "global") {
            auto it = m_env.globals.find(field);
            if (it == m_env.globals.end()) {
                auto candidates = mapKeys(m_env.globals);
                std::string msg = appendSuggestion(
                    "unknown identifier 'global." + field + "'",
                    field, candidates, m_env.suggestion_budget, "global.");
                fail("V-EXPR-2", node.line, node.column, std::move(msg),
                     OffendingSymbolKind::Global, field);
                return ExprType::number();
            }
            node.access_kind = ResolvedAccessKind::GlobalAccess;
            node.resolved_key = field;
            node.resolved_type = it->second.type;
            addUnique(m_compiled.referenced_globals, field);
            return node.resolved_type;
        }

        // 3. machine.<field>
        if (root == "machine") {
            if (m_env.context == ExprContext::GlobalStatic) {
                fail("V-EXPR-3", node.line, node.column,
                     "'machine." + field + "' is not available in global static expressions");
                return ExprType::number();
            }
            if (field == "id") {
                node.access_kind = ResolvedAccessKind::MachineId;
                node.resolved_type = ExprType::string();
                return node.resolved_type;
            }
            if (field == "speed") {
                node.access_kind = ResolvedAccessKind::MachineSpeed;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            auto it = m_env.machine_attrs.find(field);
            if (it != m_env.machine_attrs.end()) {
                node.access_kind = ResolvedAccessKind::MachineAttr;
                node.resolved_key = field;
                node.resolved_type = attrTypeToExprType(it->second);
                addUnique(m_compiled.referenced_machine_attrs, field);
                return node.resolved_type;
            }
            std::vector<std::string> candidates = mapKeys(m_env.machine_attrs);
            candidates.push_back("id");
            candidates.push_back("speed");
            std::sort(candidates.begin(), candidates.end());
            std::string msg = appendSuggestion(
                "unknown identifier 'machine." + field + "'",
                field, candidates, m_env.suggestion_budget, "machine.");
            fail("V-EXPR-2", node.line, node.column, std::move(msg),
                 OffendingSymbolKind::Attribute, field, "machine");
            return ExprType::number();
        }

        // 4. recipe.<field>
        if (root == "recipe") {
            if (m_env.context == ExprContext::GlobalStatic) {
                fail("V-EXPR-3", node.line, node.column,
                     "'recipe." + field + "' is not available in global static expressions");
                return ExprType::number();
            }
            if (field == "id") {
                node.access_kind = ResolvedAccessKind::RecipeId;
                node.resolved_type = ExprType::string();
                return node.resolved_type;
            }
            if (field == "time") {
                node.access_kind = ResolvedAccessKind::RecipeTime;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "time_raw") {
                node.access_kind = ResolvedAccessKind::RecipeTimeRaw;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "input_items_total") {
                node.access_kind = ResolvedAccessKind::RecipeInputItemsTotal;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "input_fluids_total") {
                node.access_kind = ResolvedAccessKind::RecipeInputFluidsTotal;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "output_items_total") {
                node.access_kind = ResolvedAccessKind::RecipeOutputItemsTotal;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "output_fluids_total") {
                node.access_kind = ResolvedAccessKind::RecipeOutputFluidsTotal;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            auto it = m_env.recipe_attrs.find(field);
            if (it != m_env.recipe_attrs.end()) {
                node.access_kind = ResolvedAccessKind::RecipeAttr;
                node.resolved_key = field;
                node.resolved_type = attrTypeToExprType(it->second);
                addUnique(m_compiled.referenced_recipe_attrs, field);
                return node.resolved_type;
            }
            std::vector<std::string> candidates = mapKeys(m_env.recipe_attrs);
            for (const char* builtin : {"id", "time", "time_raw",
                                        "input_items_total", "input_fluids_total",
                                        "output_items_total", "output_fluids_total"}) {
                candidates.emplace_back(builtin);
            }
            std::sort(candidates.begin(), candidates.end());
            std::string msg = appendSuggestion(
                "unknown identifier 'recipe." + field + "'",
                field, candidates, m_env.suggestion_budget, "recipe.");
            fail("V-EXPR-2", node.line, node.column, std::move(msg),
                 OffendingSymbolKind::Attribute, field, "recipe");
            return ExprType::number();
        }

        // 5. param.<id> (V-EXPR-5)
        if (root == "param") {
            if (m_env.context == ExprContext::GlobalStatic) {
                fail("V-EXPR-3", node.line, node.column,
                     "'param." + field + "' is not available in global static expressions");
                return ExprType::number();
            }

            if (m_env.check_profile_params) {
                auto prof_it = std::find(
                    m_env.profile_params.begin(), m_env.profile_params.end(), field);
                if (prof_it == m_env.profile_params.end()) {
                    std::vector<std::string> candidates = m_env.profile_params;
                    if (candidates.empty()) {
                        candidates = mapKeys(m_env.parameters);
                    }
                    std::string base = "unknown identifier 'param." + field + "'";
                    if (!m_env.profile_id.empty()) {
                        base += " (not a parameter of profile '" + m_env.profile_id + "')";
                    }
                    std::string msg = appendSuggestion(
                         std::move(base), field, candidates, m_env.suggestion_budget, "param.");
                    fail("V-EXPR-5", node.line, node.column, std::move(msg),
                         OffendingSymbolKind::Parameter, field);
                    return ExprType::number();
                }

                size_t param_idx = static_cast<size_t>(
                    std::distance(m_env.profile_params.begin(), prof_it));
                if (m_env.context == ExprContext::ParamStatic &&
                    m_env.max_param_index.has_value() &&
                    param_idx >= *m_env.max_param_index) {
                    fail("V-EXPR-5", node.line, node.column,
                         "'param." + field +
                             "' must be listed earlier in profile 'params' to be referenced in a static parameter expression",
                         OffendingSymbolKind::Parameter, field);
                    return ExprType::number();
                }
            }

            auto param_it = m_env.parameters.find(field);
            if (param_it == m_env.parameters.end()) {
                auto candidates = mapKeys(m_env.parameters);
                std::string msg = appendSuggestion(
                    "unknown identifier 'param." + field + "'",
                    field, candidates, m_env.suggestion_budget, "param.");
                fail("V-EXPR-5", node.line, node.column, std::move(msg),
                     OffendingSymbolKind::Parameter, field);
                return ExprType::number();
            }

            if (!m_env.check_profile_params &&
                m_env.context == ExprContext::ParamStatic &&
                !m_env.current_param_id.empty() &&
                field == m_env.current_param_id) {
                fail("V-EXPR-5", node.line, node.column,
                     "'param." + field + "' cannot reference itself in its own static expression",
                     OffendingSymbolKind::Parameter, field);
                return ExprType::number();
            }

            node.access_kind = ResolvedAccessKind::ParamAccess;
            node.resolved_key = field;
            node.resolved_type = param_it->second.type;
            addUnique(m_compiled.referenced_params, field);
            return node.resolved_type;
        }

        // 6. port.<field>
        if (root == "port") {
            if (!isPortAllowed()) {
                fail("V-EXPR-3", node.line, node.column,
                     "'port." + field + "' is only available in recipe port formulas and port-bound resource_choice conditions");
                return ExprType::number();
            }
            m_compiled.references_port = true;

            if (field == "dir") {
                node.access_kind = ResolvedAccessKind::PortDir;
                node.resolved_type = ExprType::string();
                return node.resolved_type;
            }
            if (field == "amount") {
                node.access_kind = ResolvedAccessKind::PortAmount;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "chance") {
                node.access_kind = ResolvedAccessKind::PortChance;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "catalyst") {
                node.access_kind = ResolvedAccessKind::PortCatalyst;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "index") {
                node.access_kind = ResolvedAccessKind::PortIndex;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "base_resource") {
                if (!m_env.port_has_base_resource) {
                    fail("V-EXPR-3", node.line, node.column,
                         "'port.base_resource' is only available when the recipe port declares a static 'res'");
                    return ExprType::number();
                }
                node.access_kind = ResolvedAccessKind::PortBaseResource;
                node.resolved_type = ExprType::resource();
                return node.resolved_type;
            }
            if (field == "resource") {
                if (!isPortResourceAllowed()) {
                    fail("V-EXPR-3", node.line, node.column,
                         "'port.resource' is only available in 'enabled' and 'amount_expr' port formulas (not in 'resource_expr' or static expressions)");
                    return ExprType::number();
                }
                m_compiled.references_port_resource = true;
                node.access_kind = ResolvedAccessKind::PortResource;
                node.resolved_type = ExprType::resource();
                return node.resolved_type;
            }
            auto it = m_env.port_attrs.find(field);
            if (it != m_env.port_attrs.end()) {
                node.access_kind = ResolvedAccessKind::PortAttr;
                node.resolved_key = field;
                node.resolved_type = attrTypeToExprType(it->second);
                addUnique(m_compiled.referenced_port_attrs, field);
                return node.resolved_type;
            }
            std::vector<std::string> candidates = mapKeys(m_env.port_attrs);
            for (const char* builtin : {"dir", "amount", "chance", "catalyst", "index", "base_resource", "resource"}) {
                candidates.emplace_back(builtin);
            }
            std::sort(candidates.begin(), candidates.end());
            std::string msg = appendSuggestion(
                "unknown identifier 'port." + field + "'",
                field, candidates, m_env.suggestion_budget, "port.");
            fail("V-EXPR-2", node.line, node.column, std::move(msg),
                 OffendingSymbolKind::Attribute, field, "port");
            return ExprType::number();
        }

        // 7. node.<field>
        if (root == "node") {
            if (m_env.context != ExprContext::PostSolveMetricValue) {
                fail("V-EXPR-3", node.line, node.column,
                     "'node." + field + "' is only available in post-solve 'value' metrics");
                return ExprType::number();
            }
            if (field == "machines") {
                node.access_kind = ResolvedAccessKind::NodeMachines;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (field == "crafts") {
                node.access_kind = ResolvedAccessKind::NodeCrafts;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            std::vector<std::string> candidates = {"crafts", "machines"};
            std::string msg = appendSuggestion(
                "unknown identifier 'node." + field + "'",
                field, candidates, m_env.suggestion_budget, "node.");
            fail("V-EXPR-2", node.line, node.column, std::move(msg),
                 OffendingSymbolKind::Identifier, "node." + field);
            return ExprType::number();
        }

        return ExprType::number();
    }

    ExprType checkMember(AstNode& node) {
        // Special §5.7 / V-EXPR-2 check: detect `.attrs.x` and suggest `.x`
        if (node.left->kind == AstKind::Member && node.left->name == "attrs") {
            AstNode& inner = *node.left;
            if (inner.left->kind == AstKind::Ident) {
                const std::string& root = inner.left->name;
                if (root == "machine" || root == "recipe" || root == "port" || root == "game") {
                    fail("V-EXPR-2", node.line, node.column,
                         "unknown identifier '" + root + ".attrs." + node.name +
                             "' (did you mean '" + root + "." + node.name + "'?)",
                         OffendingSymbolKind::Attribute, node.name, root);
                    return ExprType::number();
                }
            }
            // Otherwise check if inner.left is an expression of Option<S>, Resource, or Slots<S>
            ExprType base_type = checkNode(*inner.left);
            if (!m_ok) {
                return ExprType::number();
            }
            if (base_type.kind == ValueKind::Option ||
                base_type.kind == ValueKind::Resource ||
                base_type.kind == ValueKind::Slots) {
                fail("V-EXPR-2", inner.field_line, inner.field_column,
                     "unknown attribute 'attrs." + node.name + "' on " + base_type.toString() +
                         " (did you mean '." + node.name + "'?)",
                     OffendingSymbolKind::Attribute, node.name);
                return ExprType::number();
            }
        }

        // Case 1: Namespace root access `root.<field>`
        if (node.left->kind == AstKind::Ident && isNamespaceRoot(node.left->name)) {
            return checkNamespaceMember(node, node.left->name, node.name);
        }

        // Case 2: Value-level postfix member access `<expr>.<field>`
        ExprType obj_type = checkNode(*node.left);
        if (!m_ok) {
            return ExprType::number();
        }
        const std::string& field = node.name;

        if (obj_type.kind == ValueKind::Option) {
            if (field == "key") {
                node.access_kind = ResolvedAccessKind::OptionKey;
                node.resolved_type = ExprType::string();
                return node.resolved_type;
            }
            auto set_it = m_env.option_sets.find(obj_type.option_set);
            if (set_it != m_env.option_sets.end()) {
                auto attr_it = set_it->second.attrs.find(field);
                if (attr_it != set_it->second.attrs.end()) {
                    node.access_kind = ResolvedAccessKind::OptionAttr;
                    node.resolved_key = field;
                    node.resolved_type = attrTypeToExprType(attr_it->second);
                    return node.resolved_type;
                }
                std::vector<std::string> candidates = mapKeys(set_it->second.attrs);
                candidates.push_back("key");
                std::sort(candidates.begin(), candidates.end());
                std::string msg = appendSuggestion(
                    "unknown attribute '" + field + "' on option set '" + obj_type.option_set + "'",
                    field, candidates, m_env.suggestion_budget);
                fail("V-EXPR-2", node.field_line, node.field_column, std::move(msg),
                     OffendingSymbolKind::Attribute, field, obj_type.option_set);
                return ExprType::number();
            }
            fail("V-EXPR-2", node.field_line, node.field_column,
                 "unknown option set '" + obj_type.option_set + "'",
                 OffendingSymbolKind::Attribute, field, obj_type.option_set);
            return ExprType::number();
        }

        if (obj_type.kind == ValueKind::Resource) {
            if (field == "id") {
                node.access_kind = ResolvedAccessKind::ResourceId;
                node.resolved_type = ExprType::string();
                return node.resolved_type;
            }
            auto attr_it = m_env.resource_attrs.find(field);
            if (attr_it != m_env.resource_attrs.end()) {
                node.access_kind = ResolvedAccessKind::ResourceAttr;
                node.resolved_key = field;
                node.resolved_type = attrTypeToExprType(attr_it->second);
                addUnique(m_compiled.referenced_resource_attrs, field);
                return node.resolved_type;
            }
            std::vector<std::string> candidates = mapKeys(m_env.resource_attrs);
            candidates.push_back("id");
            std::sort(candidates.begin(), candidates.end());
            std::string msg = appendSuggestion(
                "unknown resource attribute '" + field + "'",
                field, candidates, m_env.suggestion_budget);
            fail("V-EXPR-2", node.field_line, node.field_column, std::move(msg),
                 OffendingSymbolKind::Attribute, field, "resource");
            return ExprType::number();
        }

        if (obj_type.kind == ValueKind::Slots) {
            auto set_it = m_env.option_sets.find(obj_type.option_set);
            if (set_it != m_env.option_sets.end()) {
                auto attr_it = set_it->second.attrs.find(field);
                if (attr_it != set_it->second.attrs.end()) {
                    if (attr_it->second != AttrType::Number) {
                        fail("V-EXPR-4", node.field_line, node.field_column,
                             "slots attribute '" + field + "' must have type 'number', got '" +
                                 attrTypeToExprType(attr_it->second).toString() + "'");
                        return ExprType::number();
                    }
                    node.access_kind = ResolvedAccessKind::SlotsAttr;
                    node.resolved_key = field;
                    node.resolved_type = ExprType::numberList();
                    return node.resolved_type;
                }
                std::vector<std::string> candidates = mapKeys(set_it->second.attrs);
                std::string msg = appendSuggestion(
                    "unknown attribute '" + field + "' on option set '" + obj_type.option_set + "'",
                    field, candidates, m_env.suggestion_budget);
                fail("V-EXPR-2", node.field_line, node.field_column, std::move(msg),
                     OffendingSymbolKind::Attribute, field, obj_type.option_set);
                return ExprType::number();
            }
            fail("V-EXPR-2", node.field_line, node.field_column,
                 "unknown option set '" + obj_type.option_set + "'",
                 OffendingSymbolKind::Attribute, field, obj_type.option_set);
            return ExprType::number();
        }

        fail("V-EXPR-4", node.field_line, node.field_column,
             "cannot access field '." + field + "' on value of type " + obj_type.toString());
        return ExprType::number();
    }

    ExprType checkUnary(AstNode& node) {
        ExprType op_type = checkNode(*node.left);
        if (!m_ok) {
            return ExprType::number();
        }
        if (node.unary_op == UnaryOpKind::Negate) {
            if (op_type.kind != ValueKind::Number) {
                fail("V-EXPR-4", node.line, node.column,
                     "unary '-' requires number operand, got " + op_type.toString());
                return ExprType::number();
            }
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }
        // UnaryOpKind::Not
        if (op_type.kind != ValueKind::Bool) {
            fail("V-EXPR-4", node.line, node.column,
                 "logical '!' requires bool operand, got " + op_type.toString());
            return ExprType::boolean();
        }
        node.resolved_type = ExprType::boolean();
        return node.resolved_type;
    }

    bool validateOptionStringLiteral(const AstNode& str_node, const std::string& option_set_id) {
        auto set_it = m_env.option_sets.find(option_set_id);
        if (set_it == m_env.option_sets.end()) {
            return true;
        }
        if (!set_it->second.hasKey(str_node.string_value)) {
            std::string base = "'" + str_node.string_value + "' is not an option of '" + option_set_id + "'";
            std::string msg = appendSuggestion(
                std::move(base), str_node.string_value,
                set_it->second.option_keys, m_env.suggestion_budget);
            fail("V-EXPR-2", str_node.line, str_node.column, std::move(msg),
                 OffendingSymbolKind::OptionKey, str_node.string_value, option_set_id);
            return false;
        }
        return true;
    }

    bool validateResourceStringLiteral(const AstNode& str_node) {
        if (m_env.resources.find(str_node.string_value) == m_env.resources.end()) {
            auto candidates = setKeys(m_env.resources);
            std::string msg = appendSuggestion(
                "unknown resource '" + str_node.string_value + "'",
                str_node.string_value, candidates, m_env.suggestion_budget);
            fail("V-EXPR-2", str_node.line, str_node.column, std::move(msg),
                 OffendingSymbolKind::Resource, str_node.string_value);
            return false;
        }
        addUnique(m_compiled.referenced_resources, str_node.string_value);
        return true;
    }

    ExprType checkBinary(AstNode& node) {
        ExprType lt = checkNode(*node.left);
        if (!m_ok) {
            return ExprType::number();
        }
        ExprType rt = checkNode(*node.right);
        if (!m_ok) {
            return ExprType::number();
        }

        switch (node.binary_op) {
            case BinaryOpKind::Or:
            case BinaryOpKind::And: {
                std::string op_name = (node.binary_op == BinaryOpKind::Or) ? "||" : "&&";
                if (lt.kind != ValueKind::Bool || rt.kind != ValueKind::Bool) {
                    fail("V-EXPR-4", node.line, node.column,
                         "operator '" + op_name + "' requires bool operands, got " +
                             lt.toString() + " and " + rt.toString());
                    return ExprType::boolean();
                }
                node.resolved_type = ExprType::boolean();
                return node.resolved_type;
            }
            case BinaryOpKind::Lt:
            case BinaryOpKind::Le:
            case BinaryOpKind::Gt:
            case BinaryOpKind::Ge: {
                if (lt.kind != ValueKind::Number || rt.kind != ValueKind::Number) {
                    fail("V-EXPR-4", node.line, node.column,
                         "relational comparison requires number operands, got " +
                             lt.toString() + " and " + rt.toString());
                    return ExprType::boolean();
                }
                node.resolved_type = ExprType::boolean();
                return node.resolved_type;
            }
            case BinaryOpKind::Eq:
            case BinaryOpKind::Ne: {
                // Allowed equality combinations per §5.3:
                // - number == number
                // - bool == bool
                // - string == string
                // - option<S> == option<S>
                // - option<S> == string or string == option<S> (literal checked against S)
                // - resource == resource
                // - resource == string or string == resource (literal checked against resources)
                bool valid = false;
                if (lt.kind == ValueKind::Number && rt.kind == ValueKind::Number) {
                    valid = true;
                } else if (lt.kind == ValueKind::Bool && rt.kind == ValueKind::Bool) {
                    valid = true;
                } else if (lt.kind == ValueKind::String && rt.kind == ValueKind::String) {
                    valid = true;
                } else if (lt.kind == ValueKind::Option && rt.kind == ValueKind::Option &&
                           lt.option_set == rt.option_set) {
                    valid = true;
                } else if (lt.kind == ValueKind::Option && rt.kind == ValueKind::String) {
                    if (node.right->kind == AstKind::StringLit &&
                        !validateOptionStringLiteral(*node.right, lt.option_set)) {
                        return ExprType::boolean();
                    }
                    valid = true;
                } else if (lt.kind == ValueKind::String && rt.kind == ValueKind::Option) {
                    if (node.left->kind == AstKind::StringLit &&
                        !validateOptionStringLiteral(*node.left, rt.option_set)) {
                        return ExprType::boolean();
                    }
                    valid = true;
                } else if (lt.kind == ValueKind::Resource && rt.kind == ValueKind::Resource) {
                    valid = true;
                } else if (lt.kind == ValueKind::Resource && rt.kind == ValueKind::String) {
                    if (node.right->kind == AstKind::StringLit &&
                        !validateResourceStringLiteral(*node.right)) {
                        return ExprType::boolean();
                    }
                    valid = true;
                } else if (lt.kind == ValueKind::String && rt.kind == ValueKind::Resource) {
                    if (node.left->kind == AstKind::StringLit &&
                        !validateResourceStringLiteral(*node.left)) {
                        return ExprType::boolean();
                    }
                    valid = true;
                }

                if (!valid) {
                    std::string op_name = (node.binary_op == BinaryOpKind::Eq) ? "==" : "!=";
                    fail("V-EXPR-4", node.line, node.column,
                         "cannot compare " + lt.toString() + " and " + rt.toString() +
                             " with '" + op_name + "'");
                    return ExprType::boolean();
                }
                node.resolved_type = ExprType::boolean();
                return node.resolved_type;
            }
            case BinaryOpKind::Add:
            case BinaryOpKind::Sub:
            case BinaryOpKind::Mul:
            case BinaryOpKind::Div:
            case BinaryOpKind::Mod:
            case BinaryOpKind::Pow: {
                if (lt.kind != ValueKind::Number || rt.kind != ValueKind::Number) {
                    fail("V-EXPR-4", node.line, node.column,
                         "arithmetic operator requires number operands, got " +
                             lt.toString() + " and " + rt.toString());
                    return ExprType::number();
                }
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
        }
        return ExprType::number();
    }

    ExprType checkCall(AstNode& node) {
        const std::string& fn = node.name;
        const size_t argc = node.args.size();

        if (!isBuiltinFunctionName(fn)) {
            std::vector<std::string> candidates;
            candidates.reserve(kBuiltinFunctionNames.size());
            for (std::string_view b : kBuiltinFunctionNames) {
                candidates.emplace_back(b);
            }
            std::string msg = appendSuggestion(
                "unknown function '" + fn + "'", fn, candidates, m_env.suggestion_budget);
            fail("V-EXPR-2", node.line, node.column, std::move(msg),
                 OffendingSymbolKind::Function, fn);
            return ExprType::number();
        }

        // 1. min / max: (number, number, ...) or (list) or (list, default)
        if (fn == "min" || fn == "max") {
            const bool is_min = (fn == "min");
            if (argc == 0) {
                fail("V-EXPR-4", node.line, node.column,
                     "'" + fn + "()' requires at least 1 argument");
                return ExprType::number();
            }
            ExprType first_type = checkNode(*node.args[0]);
            if (!m_ok) {
                return ExprType::number();
            }
            if (first_type.kind == ValueKind::NumberList) {
                if (argc > 2) {
                    fail("V-EXPR-4", node.line, node.column,
                         "list form of '" + fn + "()' takes 1 or 2 arguments, got " +
                             std::to_string(argc));
                    return ExprType::number();
                }
                if (argc == 2) {
                    ExprType def_type = checkNode(*node.args[1]);
                    if (!m_ok) {
                        return ExprType::number();
                    }
                    if (def_type.kind != ValueKind::Number) {
                        fail("V-EXPR-4", node.args[1]->line, node.args[1]->column,
                             "default value in '" + fn + "(list, default)' must be number, got " +
                                 def_type.toString());
                        return ExprType::number();
                    }
                }
                node.builtin_func = is_min ? BuiltinFunc::MinList : BuiltinFunc::MaxList;
                node.resolved_type = ExprType::number();
                return node.resolved_type;
            }
            if (first_type.kind != ValueKind::Number) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "'" + fn + "()' expects number or list<number>, got " +
                         first_type.toString());
                return ExprType::number();
            }
            for (size_t i = 1; i < argc; ++i) {
                ExprType t = checkNode(*node.args[i]);
                if (!m_ok) {
                    return ExprType::number();
                }
                if (t.kind != ValueKind::Number) {
                    fail("V-EXPR-4", node.args[i]->line, node.args[i]->column,
                         "'" + fn + "()' argument " + std::to_string(i + 1) +
                             " must be number, got " + t.toString());
                    return ExprType::number();
                }
            }
            node.builtin_func = is_min ? BuiltinFunc::MinScalar : BuiltinFunc::MaxScalar;
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // 2. clamp(x, lo, hi)
        if (fn == "clamp") {
            if (argc != 3) {
                fail("V-EXPR-4", node.line, node.column,
                     "'clamp()' requires 3 arguments, got " + std::to_string(argc));
                return ExprType::number();
            }
            for (size_t i = 0; i < 3; ++i) {
                ExprType t = checkNode(*node.args[i]);
                if (!m_ok) {
                    return ExprType::number();
                }
                if (t.kind != ValueKind::Number) {
                    fail("V-EXPR-4", node.args[i]->line, node.args[i]->column,
                         "'clamp()' argument " + std::to_string(i + 1) +
                             " must be number, got " + t.toString());
                    return ExprType::number();
                }
            }
            node.builtin_func = BuiltinFunc::Clamp;
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // 3. Single-argument numeric functions: abs, floor, ceil, round, sqrt, exp, ln
        if (fn == "abs" || fn == "floor" || fn == "ceil" || fn == "round" ||
            fn == "sqrt" || fn == "exp" || fn == "ln") {
            if (argc != 1) {
                fail("V-EXPR-4", node.line, node.column,
                     "'" + fn + "()' requires 1 argument, got " + std::to_string(argc));
                return ExprType::number();
            }
            ExprType t = checkNode(*node.args[0]);
            if (!m_ok) {
                return ExprType::number();
            }
            if (t.kind != ValueKind::Number) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "'" + fn + "()' requires number argument, got " + t.toString());
                return ExprType::number();
            }
            if (fn == "abs") node.builtin_func = BuiltinFunc::Abs;
            else if (fn == "floor") node.builtin_func = BuiltinFunc::Floor;
            else if (fn == "ceil") node.builtin_func = BuiltinFunc::Ceil;
            else if (fn == "round") node.builtin_func = BuiltinFunc::Round;
            else if (fn == "sqrt") node.builtin_func = BuiltinFunc::Sqrt;
            else if (fn == "exp") node.builtin_func = BuiltinFunc::Exp;
            else node.builtin_func = BuiltinFunc::Ln;
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // 4. Two-argument numeric functions: pow(a, b), log(x, base)
        if (fn == "pow" || fn == "log") {
            if (argc != 2) {
                fail("V-EXPR-4", node.line, node.column,
                     "'" + fn + "()' requires 2 arguments, got " + std::to_string(argc));
                return ExprType::number();
            }
            for (size_t i = 0; i < 2; ++i) {
                ExprType t = checkNode(*node.args[i]);
                if (!m_ok) {
                    return ExprType::number();
                }
                if (t.kind != ValueKind::Number) {
                    fail("V-EXPR-4", node.args[i]->line, node.args[i]->column,
                         "'" + fn + "()' argument " + std::to_string(i + 1) +
                             " must be number, got " + t.toString());
                    return ExprType::number();
                }
            }
            node.builtin_func = (fn == "pow") ? BuiltinFunc::Pow : BuiltinFunc::Log;
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // 5. if(cond, then_expr, else_expr)
        if (fn == "if") {
            if (argc != 3) {
                fail("V-EXPR-4", node.line, node.column,
                     "'if()' requires 3 arguments, got " + std::to_string(argc));
                return ExprType::number();
            }
            ExprType cond_t = checkNode(*node.args[0]);
            if (!m_ok) {
                return ExprType::number();
            }
            if (cond_t.kind != ValueKind::Bool) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "'if()' condition must be bool, got " + cond_t.toString());
                return ExprType::number();
            }
            ExprType then_t = checkNode(*node.args[1]);
            if (!m_ok) {
                return ExprType::number();
            }
            ExprType else_t = checkNode(*node.args[2]);
            if (!m_ok) {
                return ExprType::number();
            }
            if (then_t != else_t) {
                fail("V-EXPR-4", node.line, node.column,
                     "'if()' branches must have identical types, got " +
                         then_t.toString() + " and " + else_t.toString());
                return ExprType::number();
            }
            node.builtin_func = BuiltinFunc::If;
            node.resolved_type = then_t;
            return node.resolved_type;
        }

        // 6. lookup(table, key) or lookup(table, key, default)
        if (fn == "lookup") {
            if (argc != 2 && argc != 3) {
                fail("V-EXPR-4", node.line, node.column,
                     "'lookup()' requires 2 or 3 arguments, got " + std::to_string(argc));
                return ExprType::number();
            }
            if (node.args[0]->kind != AstKind::StringLit) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "first argument of 'lookup()' must be a string literal naming a table");
                return ExprType::number();
            }
            node.args[0]->resolved_type = ExprType::string();
            const std::string& table_name = node.args[0]->string_value;
            auto tbl_it = m_env.tables.find(table_name);
            if (tbl_it == m_env.tables.end()) {
                auto candidates = mapKeys(m_env.tables);
                std::string msg = appendSuggestion(
                    "unknown table '" + table_name + "'",
                    table_name, candidates, m_env.suggestion_budget);
                fail("V-EXPR-2", node.args[0]->line, node.args[0]->column, std::move(msg),
                     OffendingSymbolKind::Table, table_name);
                return ExprType::number();
            }
            addUnique(m_compiled.referenced_tables, table_name);

            ExprType key_t = checkNode(*node.args[1]);
            if (!m_ok) {
                return ExprType::number();
            }
            if (tbl_it->second == TableKind::Array) {
                if (key_t.kind != ValueKind::Number) {
                    fail("V-EXPR-4", node.args[1]->line, node.args[1]->column,
                         "array table '" + table_name + "' requires number index, got " +
                             key_t.toString());
                    return ExprType::number();
                }
                node.builtin_func = BuiltinFunc::LookupArray;
            } else {
                if (key_t.kind != ValueKind::String &&
                    key_t.kind != ValueKind::Option &&
                    key_t.kind != ValueKind::Resource) {
                    fail("V-EXPR-4", node.args[1]->line, node.args[1]->column,
                         "object table '" + table_name +
                             "' requires string, option, or resource key, got " +
                             key_t.toString());
                    return ExprType::number();
                }
                node.builtin_func = BuiltinFunc::LookupObject;
            }

            if (argc == 3) {
                ExprType def_t = checkNode(*node.args[2]);
                if (!m_ok) {
                    return ExprType::number();
                }
                if (def_t.kind != ValueKind::Number) {
                    fail("V-EXPR-4", node.args[2]->line, node.args[2]->column,
                         "'lookup()' default value must be number, got " + def_t.toString());
                    return ExprType::number();
                }
            }
            node.resolved_key = table_name;
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // 7. has_tag(resource, string)
        if (fn == "has_tag") {
            if (argc != 2) {
                fail("V-EXPR-4", node.line, node.column,
                     "'has_tag()' requires 2 arguments, got " + std::to_string(argc));
                return ExprType::boolean();
            }
            ExprType res_t = checkNode(*node.args[0]);
            if (!m_ok) {
                return ExprType::boolean();
            }
            if (res_t.kind != ValueKind::Resource) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "first argument of 'has_tag()' must be resource, got " + res_t.toString());
                return ExprType::boolean();
            }
            ExprType tag_t = checkNode(*node.args[1]);
            if (!m_ok) {
                return ExprType::boolean();
            }
            if (tag_t.kind != ValueKind::String) {
                fail("V-EXPR-4", node.args[1]->line, node.args[1]->column,
                     "second argument of 'has_tag()' must be string, got " + tag_t.toString());
                return ExprType::boolean();
            }
            node.builtin_func = BuiltinFunc::HasTag;
            node.resolved_type = ExprType::boolean();
            return node.resolved_type;
        }

        // 8. sum(list)
        if (fn == "sum") {
            if (argc != 1) {
                fail("V-EXPR-4", node.line, node.column,
                     "'sum()' requires 1 argument, got " + std::to_string(argc));
                return ExprType::number();
            }
            ExprType list_t = checkNode(*node.args[0]);
            if (!m_ok) {
                return ExprType::number();
            }
            if (list_t.kind != ValueKind::NumberList) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "'sum()' requires list<number> argument, got " + list_t.toString());
                return ExprType::number();
            }
            node.builtin_func = BuiltinFunc::Sum;
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // 9. count(slots) or count(slots, "key")
        if (fn == "count") {
            if (argc != 1 && argc != 2) {
                fail("V-EXPR-4", node.line, node.column,
                     "'count()' requires 1 or 2 arguments, got " + std::to_string(argc));
                return ExprType::number();
            }
            ExprType slots_t = checkNode(*node.args[0]);
            if (!m_ok) {
                return ExprType::number();
            }
            if (slots_t.kind != ValueKind::Slots) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "first argument of 'count()' must be a slots parameter or global, got " +
                         slots_t.toString());
                return ExprType::number();
            }
            if (argc == 2) {
                if (node.args[1]->kind != AstKind::StringLit) {
                    fail("V-EXPR-4", node.args[1]->line, node.args[1]->column,
                         "second argument of 'count(slots, \"key\")' must be an option key string literal");
                    return ExprType::number();
                }
                node.args[1]->resolved_type = ExprType::string();
                if (!validateOptionStringLiteral(*node.args[1], slots_t.option_set)) {
                    return ExprType::number();
                }
                node.builtin_func = BuiltinFunc::CountKey;
            } else {
                node.builtin_func = BuiltinFunc::CountAll;
            }
            node.resolved_type = ExprType::number();
            return node.resolved_type;
        }

        // 10. is_set(param.x) or is_set(global.x)
        if (fn == "is_set") {
            if (argc != 1) {
                fail("V-EXPR-4", node.line, node.column,
                     "'is_set()' requires 1 argument, got " + std::to_string(argc));
                return ExprType::boolean();
            }
            ExprType arg_t = checkNode(*node.args[0]);
            if (!m_ok) {
                return ExprType::boolean();
            }
            const ResolvedAccessKind ak = node.args[0]->access_kind;
            if (ak != ResolvedAccessKind::ParamAccess &&
                ak != ResolvedAccessKind::GlobalAccess) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "argument of 'is_set()' must be a bare 'param.<id>' or 'global.<id>'");
                return ExprType::boolean();
            }
            if (arg_t.kind != ValueKind::Option &&
                arg_t.kind != ValueKind::Resource &&
                arg_t.kind != ValueKind::Slots) {
                fail("V-EXPR-4", node.args[0]->line, node.args[0]->column,
                     "'is_set()' only applies to enum, resource_choice, or slots parameters/globals, got " +
                         arg_t.toString());
                return ExprType::boolean();
            }
            node.builtin_func = (ak == ResolvedAccessKind::ParamAccess)
                ? BuiltinFunc::IsSetParam
                : BuiltinFunc::IsSetGlobal;
            node.resolved_key = node.args[0]->resolved_key;
            node.resolved_type = ExprType::boolean();
            return node.resolved_type;
        }

        return ExprType::number();
    }

    std::string_view m_source;
    const CompileEnv& m_env;
    CompiledExpr& m_compiled;
    bool m_ok = true;
    CompileError m_error;
};

} // namespace

CompileResult compile(
    std::string_view source,
    const CompileEnv& env,
    ExpectedType expected_type) {
    CompileResult res;
    ParseResult parsed = parse(source);
    if (!parsed.ok) {
        res.ok = false;
        res.error = std::move(parsed.error);
        return res;
    }

    res.expr.source = std::string(source);
    res.expr.root = std::move(parsed.root);

    TypeChecker checker(source, env, res.expr);
    if (!checker.checkRoot(*res.expr.root, expected_type)) {
        res.ok = false;
        res.error = checker.error();
        res.expr.root.reset();
        return res;
    }

    res.ok = true;
    return res;
}

// ============================================================================
// 4. Topological Sort & Cycle Detection (let & globals, V-EXPR-6)
// ============================================================================

DependencyOrderResult resolveDependencyOrder(
    const std::unordered_map<std::string, std::vector<std::string>>& deps_by_node) {
    DependencyOrderResult result;
    if (deps_by_node.empty()) {
        return result;
    }

    // Collect all nodes in sorted order
    std::vector<std::string> all_nodes;
    all_nodes.reserve(deps_by_node.size());
    for (const auto& [u, _] : deps_by_node) {
        all_nodes.push_back(u);
    }
    std::sort(all_nodes.begin(), all_nodes.end());
    std::unordered_set<std::string> node_set(all_nodes.begin(), all_nodes.end());

    // Build evaluation graph: if u depends on v, then v -> u in evaluation order
    // (in_degree[u] = number of distinct dependencies v in node_set)
    std::unordered_map<std::string, size_t> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;     // v -> list of u that depend on v
    std::unordered_map<std::string, std::vector<std::string>> clean_deps_map; // u -> sorted unique v in node_set

    for (const std::string& u : all_nodes) {
        in_degree[u] = 0;
    }

    for (const auto& [u, raw_deps] : deps_by_node) {
        std::vector<std::string> unique_deps;
        for (const std::string& v : raw_deps) {
            if (node_set.count(v)) {
                addUnique(unique_deps, v);
            }
        }
        std::sort(unique_deps.begin(), unique_deps.end());
        in_degree[u] = unique_deps.size();
        for (const std::string& v : unique_deps) {
            dependents[v].push_back(u);
        }
        clean_deps_map[u] = std::move(unique_deps);
    }

    // Kahn's algorithm with lexicographical min-heap to break ties by name
    std::priority_queue<std::string, std::vector<std::string>, std::greater<std::string>> ready;
    for (const std::string& u : all_nodes) {
        if (in_degree[u] == 0) {
            ready.push(u);
        }
    }

    std::unordered_set<std::string> visited;
    while (!ready.empty()) {
        std::string u = ready.top();
        ready.pop();
        result.order.push_back(u);
        visited.insert(u);

        auto it = dependents.find(u);
        if (it != dependents.end()) {
            for (const std::string& w : it->second) {
                if (--in_degree[w] == 0) {
                    ready.push(w);
                }
            }
        }
    }

    if (result.order.size() == all_nodes.size()) {
        result.ok = true;
        return result;
    }

    result.ok = false;

    // Separate unvisited nodes into:
    // - cycled_nodes: nodes that lie on an actual cycle (Tarjan SCC with size > 1 or self-loop u -> u)
    // - downstream_of_cycle: nodes not on a cycle that transitively depend on a cycle
    std::unordered_map<std::string, int> disc;
    std::unordered_map<std::string, int> low;
    std::unordered_set<std::string> on_stack;
    std::vector<std::string> st;
    int timer = 0;

    std::unordered_map<std::string, int> node_scc_id;
    std::vector<std::vector<std::string>> sccs;

    std::function<void(const std::string&)> dfs = [&](const std::string& u) {
        disc[u] = low[u] = ++timer;
        st.push_back(u);
        on_stack.insert(u);

        for (const std::string& v : clean_deps_map[u]) {
            if (visited.count(v)) {
                continue;
            }
            if (!disc.count(v)) {
                dfs(v);
                low[u] = std::min(low[u], low[v]);
            } else if (on_stack.count(v)) {
                low[u] = std::min(low[u], disc[v]);
            }
        }

        if (low[u] == disc[u]) {
            int scc_idx = static_cast<int>(sccs.size());
            std::vector<std::string> comp;
            while (true) {
                std::string w = st.back();
                st.pop_back();
                on_stack.erase(w);
                node_scc_id[w] = scc_idx;
                comp.push_back(w);
                if (w == u) {
                    break;
                }
            }
            std::sort(comp.begin(), comp.end());
            sccs.push_back(std::move(comp));
        }
    };

    for (const std::string& u : all_nodes) {
        if (!visited.count(u) && !disc.count(u)) {
            dfs(u);
        }
    }

    std::unordered_set<std::string> cycled_set;
    for (const auto& comp : sccs) {
        bool is_cycle = false;
        if (comp.size() > 1) {
            is_cycle = true;
        } else if (comp.size() == 1) {
            const std::string& u = comp[0];
            const auto& d = clean_deps_map[u];
            if (std::find(d.begin(), d.end(), u) != d.end()) {
                is_cycle = true; // Self-loop u -> u
            }
        }
        if (is_cycle) {
            for (const std::string& u : comp) {
                cycled_set.insert(u);
                result.cycled_nodes.push_back(u);
            }
        }
    }
    std::sort(result.cycled_nodes.begin(), result.cycled_nodes.end());

    for (const std::string& u : all_nodes) {
        if (!visited.count(u) && !cycled_set.count(u)) {
            result.downstream_of_cycle.push_back(u);
        }
    }
    std::sort(result.downstream_of_cycle.begin(), result.downstream_of_cycle.end());

    // Reconstruct canonical shortest cycle starting from the lexicographically smallest cycled node
    if (!result.cycled_nodes.empty()) {
        const std::string& start = result.cycled_nodes.front();
        int target_scc = node_scc_id[start];

        // Check self-loop first
        const auto& start_deps = clean_deps_map[start];
        if (std::find(start_deps.begin(), start_deps.end(), start) != start_deps.end()) {
            result.canonical_cycle = {start, start};
        } else {
            // BFS along dependency edges within the same SCC
            std::queue<std::string> q;
            std::unordered_map<std::string, std::string> parent;
            bool found = false;

            for (const std::string& nxt : start_deps) {
                if (node_scc_id.count(nxt) && node_scc_id[nxt] == target_scc) {
                    parent[nxt] = start;
                    q.push(nxt);
                }
            }

            while (!q.empty() && !found) {
                std::string curr = q.front();
                q.pop();
                for (const std::string& nxt : clean_deps_map[curr]) {
                    if (!node_scc_id.count(nxt) || node_scc_id[nxt] != target_scc) {
                        continue;
                    }
                    if (nxt == start) {
                        // Reconstruct path start -> ... -> curr -> start
                        std::vector<std::string> rev_path;
                        rev_path.push_back(start);
                        std::string p = curr;
                        while (p != start) {
                            rev_path.push_back(p);
                            p = parent[p];
                        }
                        rev_path.push_back(start);
                        std::reverse(rev_path.begin(), rev_path.end());
                        result.canonical_cycle = std::move(rev_path);
                        found = true;
                        break;
                    }
                    if (!parent.count(nxt)) {
                        parent[nxt] = curr;
                        q.push(nxt);
                    }
                }
            }
        }

        std::ostringstream oss;
        oss << "cycle ";
        for (size_t i = 0; i < result.canonical_cycle.size(); ++i) {
            if (i > 0) {
                oss << " -> ";
            }
            oss << result.canonical_cycle[i];
        }
        result.cycle_message = oss.str();
    }

    return result;
}

namespace {

void collectBareIdentReferences(
    const AstNode& node,
    const std::unordered_set<std::string>& candidate_names,
    std::vector<std::string>& out_refs) {
    switch (node.kind) {
        case AstKind::Ident:
            if (candidate_names.count(node.name)) {
                addUnique(out_refs, node.name);
            }
            break;
        case AstKind::Member:
            if (node.left) {
                collectBareIdentReferences(*node.left, candidate_names, out_refs);
            }
            break;
        case AstKind::UnaryOp:
            if (node.left) {
                collectBareIdentReferences(*node.left, candidate_names, out_refs);
            }
            break;
        case AstKind::BinaryOp:
            if (node.left) {
                collectBareIdentReferences(*node.left, candidate_names, out_refs);
            }
            if (node.right) {
                collectBareIdentReferences(*node.right, candidate_names, out_refs);
            }
            break;
        case AstKind::Call:
            for (const auto& arg : node.args) {
                if (arg) {
                    collectBareIdentReferences(*arg, candidate_names, out_refs);
                }
            }
            break;
        default:
            break;
    }
}

} // namespace

LetCompileResult compileLetMap(
    const std::unordered_map<std::string, std::string>& base_lets,
    const CompileEnv& base_env,
    const std::unordered_map<std::string, std::string>& override_lets,
    const std::unordered_map<std::string, ExprType>* required_base_types) {
    LetCompileResult res;

    // Merge base_lets with override_lets, checking V-OVR-1 for unknown override names
    std::unordered_map<std::string, std::string> effective_lets = base_lets;
    for (const auto& [ovr_name, ovr_expr] : override_lets) {
        if (base_lets.find(ovr_name) == base_lets.end()) {
            res.ok = false;
            res.errors.push_back(makeError(
                "V-OVR-1", 1, 1,
                "override let variable '" + ovr_name + "' is not defined in profile" +
                    (base_env.profile_id.empty() ? "" : " '" + base_env.profile_id + "'"),
                ovr_expr, OffendingSymbolKind::Let, ovr_name));
        } else {
            effective_lets[ovr_name] = ovr_expr;
        }
    }

    if (effective_lets.empty()) {
        return res;
    }

    std::unordered_set<std::string> let_names;
    for (const auto& [name, _] : effective_lets) {
        let_names.insert(name);
    }

    // Parse all let expressions and extract dependencies among let variables
    std::unordered_map<std::string, std::vector<std::string>> deps_by_let;
    std::vector<std::string> sorted_names = mapKeys(effective_lets);

    for (const std::string& name : sorted_names) {
        const std::string& expr_src = effective_lets.at(name);
        ParseResult parsed = parse(expr_src);
        if (!parsed.ok) {
            res.ok = false;
            res.errors.push_back(std::move(parsed.error));
            deps_by_let[name] = {};
            continue;
        }
        std::vector<std::string> refs;
        collectBareIdentReferences(*parsed.root, let_names, refs);
        deps_by_let[name] = std::move(refs);
    }

    res.dep_order = resolveDependencyOrder(deps_by_let);
    if (!res.dep_order.ok) {
        res.ok = false;
        std::string rule = override_lets.empty() ? "V-EXPR-6" : "V-OVR-2";
        res.errors.push_back(makeError(
            std::move(rule), 1, 1,
            res.dep_order.cycle_message, {},
            OffendingSymbolKind::Let,
            res.dep_order.cycled_nodes.empty() ? std::string{} : res.dep_order.cycled_nodes.front()));
        return res;
    }

    if (!res.ok) {
        return res;
    }

    // Compile let expressions in topological order so upstream let types are in scope
    CompileEnv env = base_env;
    env.context = ExprContext::PreSolveLet;
    env.let_types.clear();

    for (const std::string& name : res.dep_order.order) {
        const std::string& expr_src = effective_lets.at(name);
        CompileResult comp = compile(expr_src, env, ExpectedType::Any);
        if (!comp.ok) {
            res.ok = false;
            res.errors.push_back(std::move(comp.error));
            return res;
        }

        // V-OVR-2: overridden let variable must preserve the base profile's result type
        if (required_base_types != nullptr && override_lets.count(name)) {
            auto req_it = required_base_types->find(name);
            if (req_it != required_base_types->end() && comp.expr.type != req_it->second) {
                res.ok = false;
                res.errors.push_back(makeError(
                    "V-OVR-2", 1, 1,
                    "override for let variable '" + name + "' changes type from " +
                        req_it->second.toString() + " to " + comp.expr.type.toString(),
                    expr_src, OffendingSymbolKind::Let, name));
                return res;
            }
        }

        env.let_types[name] = comp.expr.type;
        res.let_types[name] = comp.expr.type;
        res.evaluation_order.push_back(name);
        res.compiled_lets.emplace(name, std::move(comp.expr));
    }

    return res;
}

} // namespace expr
