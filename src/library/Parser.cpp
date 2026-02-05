#include <protoPython/Parser.h>
#include <stdexcept>

namespace protoPython {

Parser::Parser(const std::string& source) : tok_(source) {
    advance();
}

void Parser::advance() {
    cur_ = tok_.next();
}

bool Parser::accept(TokenType t) {
    if (cur_.type == t) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenType t) {
    if (cur_.type == t) {
        advance();
        return true;
    }
    return false;
}

void Parser::skipNewlines() {
    while (cur_.type == TokenType::Newline)
        advance();
}

std::unique_ptr<ASTNode> Parser::parseSubscript() {
    if (cur_.type == TokenType::RSquare) {
        advance();
        return nullptr;
    }
    std::unique_ptr<ASTNode> first;
    if (cur_.type != TokenType::Colon)
        first = parseOrExpr();
    if (first && !accept(TokenType::Colon)) {
        expect(TokenType::RSquare);
        return first;
    }
    /* Slice: [start:] [stop] [ step] */
    auto sl = std::make_unique<SliceNode>();
    sl->start = std::move(first);
    if (cur_.type != TokenType::Colon && cur_.type != TokenType::RSquare)
        sl->stop = parseOrExpr();
    if (accept(TokenType::Colon) && cur_.type != TokenType::RSquare)
        sl->step = parseOrExpr();
    expect(TokenType::RSquare);
    return sl;
}

std::unique_ptr<ASTNode> Parser::parseAtom() {
    if (cur_.type == TokenType::Number) {
        auto n = std::make_unique<ConstantNode>();
        n->constType = cur_.isInteger ? ConstantNode::ConstType::Int : ConstantNode::ConstType::Float;
        n->intVal = cur_.intValue;
        n->floatVal = cur_.numValue;
        advance();
        return n;
    }
    if (cur_.type == TokenType::String) {
        auto n = std::make_unique<ConstantNode>();
        n->constType = ConstantNode::ConstType::Str;
        n->strVal = cur_.value;
        advance();
        return n;
    }
    if (cur_.type == TokenType::Name) {
        std::string name = cur_.value;
        advance();
        if (cur_.type == TokenType::LParen) {
            advance();
            auto call = std::make_unique<CallNode>();
            call->func = std::make_unique<NameNode>();
            static_cast<NameNode*>(call->func.get())->id = name;
            while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
                call->args.push_back(parseOrExpr());
                if (!accept(TokenType::Comma))
                    break;
            }
            expect(TokenType::RParen);
            return call;
        }
        auto n = std::make_unique<NameNode>();
        n->id = name;
        return n;
    }
    if (accept(TokenType::LParen)) {
        auto e = parseOrExpr();
        if (accept(TokenType::Comma)) {
            auto tup = std::make_unique<TupleLiteralNode>();
            tup->elements.push_back(std::move(e));
            while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
                auto next = parseOrExpr();
                if (!next) break;
                tup->elements.push_back(std::move(next));
                if (!accept(TokenType::Comma)) break;
            }
            expect(TokenType::RParen);
            return tup;
        }
        expect(TokenType::RParen);
        return e;
    }
    if (accept(TokenType::LSquare)) {
        auto lst = std::make_unique<ListLiteralNode>();
        while (cur_.type != TokenType::RSquare && cur_.type != TokenType::EndOfFile) {
            lst->elements.push_back(parseOrExpr());
            if (!accept(TokenType::Comma)) break;
        }
        expect(TokenType::RSquare);
        return lst;
    }
    if (accept(TokenType::LCurly)) {
        auto d = std::make_unique<DictLiteralNode>();
        while (cur_.type != TokenType::RCurly && cur_.type != TokenType::EndOfFile) {
            auto k = parseOrExpr();
            if (!expect(TokenType::Colon)) return nullptr;
            auto v = parseOrExpr();
            d->keys.push_back(std::move(k));
            d->values.push_back(std::move(v));
            if (!accept(TokenType::Comma)) break;
        }
        expect(TokenType::RCurly);
        return d;
    }
    return nullptr;
}

std::unique_ptr<ASTNode> Parser::parsePrimary() {
    auto left = parseAtom();
    if (!left) return nullptr;
    for (;;) {
        if (accept(TokenType::Dot)) {
            if (cur_.type != TokenType::Name) return left;
            auto att = std::make_unique<AttributeNode>();
            att->value = std::move(left);
            att->attr = cur_.value;
            advance();
            left = std::move(att);
            continue;
        }
        if (accept(TokenType::LSquare)) {
            auto sub = parseSubscript();
            if (!sub) return left;
            auto subn = std::make_unique<SubscriptNode>();
            subn->value = std::move(left);
            subn->index = std::move(sub);
            left = std::move(subn);
            continue;
        }
        if (cur_.type == TokenType::LParen) {
            advance();
            auto call = std::make_unique<CallNode>();
            call->func = std::move(left);
            while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
                call->args.push_back(parseOrExpr());
                if (!accept(TokenType::Comma)) break;
            }
            expect(TokenType::RParen);
            left = std::move(call);
            continue;
        }
        break;
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseUnary() {
    if (cur_.type == TokenType::Minus) {
        advance();
        auto u = parseUnary();
        if (!u) return nullptr;
        auto bin = std::make_unique<BinOpNode>();
        bin->left = std::make_unique<ConstantNode>();
        static_cast<ConstantNode*>(bin->left.get())->intVal = 0;
        static_cast<ConstantNode*>(bin->left.get())->constType = ConstantNode::ConstType::Int;
        bin->op = TokenType::Minus;
        bin->right = std::move(u);
        return bin;
    }
    return parsePrimary();
}

std::unique_ptr<ASTNode> Parser::parseMulExpr() {
    auto left = parseUnary();
    if (!left) return nullptr;
    while (cur_.type == TokenType::Star || cur_.type == TokenType::Slash) {
        TokenType op = cur_.type;
        advance();
        auto right = parseUnary();
        if (!right) return left;
        auto bin = std::make_unique<BinOpNode>();
        bin->left = std::move(left);
        bin->op = op;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseAddExpr() {
    auto left = parseMulExpr();
    if (!left) return nullptr;
    while (cur_.type == TokenType::Plus || cur_.type == TokenType::Minus) {
        TokenType op = cur_.type;
        advance();
        auto right = parseMulExpr();
        if (!right) return left;
        auto bin = std::make_unique<BinOpNode>();
        bin->left = std::move(left);
        bin->op = op;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseOrExpr() {
    return parseAddExpr();
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    skipNewlines();
    if (cur_.type == TokenType::Pass) {
        advance();
        return std::make_unique<PassNode>();
    }
    if (cur_.type == TokenType::Def) {
        advance();
        if (cur_.type != TokenType::Name) return nullptr;
        std::string funcName = cur_.value;
        advance();
        if (!expect(TokenType::LParen)) return nullptr;
        auto fn = std::make_unique<FunctionDefNode>();
        fn->name = funcName;
        while (cur_.type == TokenType::Name) {
            fn->parameters.push_back(cur_.value);
            advance();
            if (!accept(TokenType::Comma)) break;
        }
        if (!expect(TokenType::RParen)) return nullptr;
        if (cur_.type != TokenType::Colon) return nullptr;
        advance();
        auto body = parseSuite();
        if (!body) return nullptr;
        fn->body = std::move(body);
        return fn;
    }
    if (cur_.type == TokenType::Global) {
        advance();
        auto g = std::make_unique<GlobalNode>();
        if (cur_.type != TokenType::Name) return nullptr;
        g->names.push_back(cur_.value);
        advance();
        while (accept(TokenType::Comma) && cur_.type == TokenType::Name) {
            g->names.push_back(cur_.value);
            advance();
        }
        return g;
    }
    if (cur_.type == TokenType::For) {
        advance();
        auto target = parsePrimary();
        if (!target || !expect(TokenType::In)) return nullptr;
        auto iter = parseOrExpr();
        if (!iter || !expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        auto f = std::make_unique<ForNode>();
        f->target = std::move(target);
        f->iter = std::move(iter);
        f->body = std::move(body);
        return f;
    }
    if (cur_.type == TokenType::If) {
        advance();
        auto test = parseOrExpr();
        if (!test || !expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        auto iff = std::make_unique<IfNode>();
        iff->test = std::move(test);
        iff->body = std::move(body);
        if (accept(TokenType::Else)) {
            if (!expect(TokenType::Colon)) return nullptr;
            iff->orelse = parseSuite();
        }
        return iff;
    }
    auto expr = parseOrExpr();
    if (!expr) return nullptr;
    if (accept(TokenType::Assign)) {
        auto value = parseOrExpr();
        if (!value) return nullptr;
        auto a = std::make_unique<AssignNode>();
        a->target = std::move(expr);
        a->value = std::move(value);
        return a;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseExpression() {
    return parseOrExpr();
}

std::unique_ptr<ASTNode> Parser::parseSuite() {
    skipNewlines();
    auto suite = std::make_unique<SuiteNode>();
    if (accept(TokenType::Indent)) {
        while (cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile) {
            auto st = parseStatement();
            if (!st) break;
            suite->statements.push_back(std::move(st));
            skipNewlines();
        }
        if (!expect(TokenType::Dedent))
            return nullptr;
    } else {
        auto st = parseStatement();
        if (st)
            suite->statements.push_back(std::move(st));
    }
    if (suite->statements.empty())
        suite->statements.push_back(std::make_unique<PassNode>());
    return suite;
}

std::unique_ptr<ModuleNode> Parser::parseModule() {
    auto mod = std::make_unique<ModuleNode>();
    skipNewlines();
    while (cur_.type != TokenType::EndOfFile) {
        auto st = parseStatement();
        if (!st) break;
        mod->body.push_back(std::move(st));
        skipNewlines();
    }
    return mod;
}

} // namespace protoPython
