#include <protoPython/Parser.h>
#include <iostream>
#include <stdexcept>

namespace protoPython {

static const char* tokenToName(TokenType t) {
    switch (t) {
        case TokenType::Number: return "number";
        case TokenType::String: return "string";
        case TokenType::Name: return "name";
        case TokenType::Plus: return "'+'";
        case TokenType::Minus: return "'-'";
        case TokenType::Star: return "'*'";
        case TokenType::Slash: return "'/'";
        case TokenType::LParen: return "'('";
        case TokenType::RParen: return "')'";
        case TokenType::Comma: return "','";
        case TokenType::Newline: return "newline";
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Dot: return "'.'";
        case TokenType::LSquare: return "'['";
        case TokenType::RSquare: return "']'";
        case TokenType::LCurly: return "'{'";
        case TokenType::RCurly: return "'}'";
        case TokenType::Colon: return "':'";
        case TokenType::Assign: return "'='";
        case TokenType::EqEqual: return "'=='";
        case TokenType::For: return "'for'";
        case TokenType::In: return "'in'";
        case TokenType::If: return "'if'";
        case TokenType::Else: return "'else'";
        case TokenType::Global: return "'global'";
        case TokenType::Def: return "'def'";
        case TokenType::Import: return "'import'";
        case TokenType::From: return "'from'";
        case TokenType::Class: return "'class'";
        case TokenType::Return: return "'return'";
        case TokenType::While: return "'while'";
        case TokenType::True: return "'True'";
        case TokenType::False: return "'False'";
        case TokenType::None: return "'None'";
        case TokenType::And: return "'and'";
        case TokenType::Or: return "'or'";
        case TokenType::Not: return "'not'";
        case TokenType::Try: return "'try'";
        case TokenType::Except: return "'except'";
        case TokenType::Finally: return "'finally'";
        case TokenType::Raise: return "'raise'";
        case TokenType::Break: return "'break'";
        case TokenType::Continue: return "'continue'";
        case TokenType::Lambda: return "'lambda'";
        case TokenType::With: return "'with'";
        case TokenType::As: return "'as'";
        case TokenType::Is: return "'is'";
        case TokenType::IsNot: return "'is_not'";
        case TokenType::NotIn: return "'not_in'";
        case TokenType::Modulo: return "'%'";
        case TokenType::NotEqual: return "'!='";
        case TokenType::Less: return "'<'";
        case TokenType::Greater: return "'>'";
        case TokenType::LessEqual: return "'<='";
        case TokenType::GreaterEqual: return "'>='";
        case TokenType::Yield: return "'yield'";
        case TokenType::Pass: return "'pass'";
        case TokenType::Indent: return "indent";
        case TokenType::Dedent: return "dedent";
        case TokenType::Semicolon: return "';'";
        default: return "unknown";
    }
}

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
    std::string msg = "expected ";
    msg += tokenToName(t);
    msg += ", but got ";
    msg += tokenToName(cur_.type);
    if (!cur_.value.empty()) msg += " ('" + cur_.value + "')";
    error(msg);
    return false;
}

void Parser::error(const std::string& msg) {
    if (!hasError_) {
        hasError_ = true;
        lastErrorMsg_ = msg;
        lastErrorLine_ = cur_.line;
        lastErrorColumn_ = cur_.column;
        if (std::getenv("PROTO_PARSER_DEBUG")) {
            std::cerr << "[proto-parser-debug] Error at line " << cur_.line << ":" << cur_.column 
                      << " token=" << tokenToName(cur_.type) << " value='" << cur_.value << "': " << msg << "\n" << std::flush;
        }
    }
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
        auto n = std::make_unique<NameNode>();
        n->id = cur_.value;
        advance();
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
    if (cur_.type == TokenType::True || cur_.type == TokenType::False) {
        auto n = std::make_unique<ConstantNode>();
        n->constType = ConstantNode::ConstType::Bool;
        n->intVal = (cur_.type == TokenType::True) ? 1 : 0;
        advance();
        return n;
    }
    if (cur_.type == TokenType::None) {
        auto n = std::make_unique<ConstantNode>();
        n->constType = ConstantNode::ConstType::None;
        advance();
        return n;
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
                if (cur_.type == TokenType::Name && tok_.peek().type == TokenType::Assign) {
                    std::string kwname = cur_.value;
                    advance(); // name
                    advance(); // =
                    call->keywords.push_back({kwname, parseOrExpr()});
                } else {
                    if (!call->keywords.empty()) {
                        error("positional argument follows keyword argument");
                    }
                    call->args.push_back(parseOrExpr());
                }
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
    while (cur_.type == TokenType::Star || cur_.type == TokenType::Slash || cur_.type == TokenType::Modulo) {
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

std::unique_ptr<ASTNode> Parser::parseCompareExpr() {
    auto left = parseAddExpr();
    if (!left) return nullptr;
    for (;;) {
        TokenType op = cur_.type;
        bool matched = false;
        if (op == TokenType::EqEqual || op == TokenType::NotEqual || 
            op == TokenType::Less || op == TokenType::LessEqual ||
            op == TokenType::Greater || op == TokenType::GreaterEqual) matched = true;
        else if (op == TokenType::Is) {
            matched = true;
            advance();
            if (cur_.type == TokenType::Not) {
                op = TokenType::IsNot; // Need to ensure IsNot exists or handle logic
                advance();
            }
        } else if (op == TokenType::In) {
            matched = true;
            advance();
        } else if (op == TokenType::Not) {
            // Check for 'not in'
            Token next = tok_.peek();
            if (next.type == TokenType::In) {
                matched = true;
                advance(); // not
                advance(); // in
                op = TokenType::NotIn;
            }
        }
        
        if (matched) {
            if (op == TokenType::EqEqual) advance();
            auto right = parseAddExpr();
            if (!right) return left;
            auto bin = std::make_unique<BinOpNode>();
            bin->left = std::move(left);
            bin->op = op;
            bin->right = std::move(right);
            left = std::move(bin);
        } else {
            break;
        }
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseAndExpr() {
    auto left = parseCompareExpr();
    if (!left) return nullptr;
    while (cur_.type == TokenType::And) {
        advance();
        auto right = parseCompareExpr();
        if (!right) return left;
        auto bin = std::make_unique<BinOpNode>();
        bin->left = std::move(left);
        bin->op = TokenType::And;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseOrExpr() {
    auto left = parseAndExpr();
    if (!left) return nullptr;
    while (cur_.type == TokenType::Or) {
        advance();
        auto right = parseAndExpr();
        if (!right) return left;
        auto bin = std::make_unique<BinOpNode>();
        bin->left = std::move(left);
        bin->op = TokenType::Or;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
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
            if (accept(TokenType::Assign)) {
                auto defaultVal = parseOrExpr();
                (void)defaultVal; // Defaults not yet supported in compiler, just skipping
            }
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
    if (cur_.type == TokenType::Return) {
        advance();
        auto ret = std::make_unique<ReturnNode>();
        if (cur_.type != TokenType::Newline && cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile) {
            ret->value = parseOrExpr();
        }
        return ret;
    }
    if (cur_.type == TokenType::Import) {
        advance();
        if (cur_.type != TokenType::Name) return nullptr;
        auto imp = std::make_unique<ImportNode>();
        imp->moduleName = cur_.value;
        advance();
        if (accept(TokenType::As)) {
            if (cur_.type != TokenType::Name) return nullptr;
            imp->alias = cur_.value;
            advance();
        } else {
            imp->alias = imp->moduleName;
        }
        return imp;
    }
    if (cur_.type == TokenType::Try) {
        advance();
        if (!expect(TokenType::Colon)) return nullptr;
        auto t = std::make_unique<TryNode>();
        t->body = parseSuite();
        while (accept(TokenType::Except)) {
            if (cur_.type == TokenType::Name) {
                advance(); // Simple skip exception type
                if (accept(TokenType::As)) {
                    if (cur_.type == TokenType::Name) advance();
                }
            }
            if (!expect(TokenType::Colon)) return nullptr;
            t->handlers = parseSuite(); // For now just keep the last one or skip
        }
        if (accept(TokenType::Finally)) {
            if (!expect(TokenType::Colon)) return nullptr;
            t->finalbody = parseSuite();
        }
        return t;
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
        do {
            auto st = parseStatement();
            if (st)
                suite->statements.push_back(std::move(st));
        } while (accept(TokenType::Semicolon) && cur_.type != TokenType::Newline && cur_.type != TokenType::EndOfFile);
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
        if (st) {
            mod->body.push_back(std::move(st));
        }
        while (accept(TokenType::Semicolon) && cur_.type != TokenType::Newline && cur_.type != TokenType::EndOfFile) {
            auto s2 = parseStatement();
            if (s2) mod->body.push_back(std::move(s2));
        }
        skipNewlines();
    }
    return mod;
}

} // namespace protoPython
