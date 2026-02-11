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
        case TokenType::Error: return "error";
        default: return "unknown";
    }
}

Parser::Parser(const std::string& source) : tok_(source) {
    advance();
}

void Parser::advance() {
    cur_ = tok_.next();
    if (cur_.type == TokenType::Error) {
        error(cur_.value);
    }
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
        first = parseExpression();
    if (first && !accept(TokenType::Colon)) {
        expect(TokenType::RSquare);
        return first;
    }
    /* Slice: [start:] [stop] [ step] */
    auto sl = std::make_unique<SliceNode>();
    sl->start = std::move(first);
    if (cur_.type != TokenType::Colon && cur_.type != TokenType::RSquare)
        sl->stop = parseExpression();
    if (accept(TokenType::Colon) && cur_.type != TokenType::RSquare)
        sl->step = parseExpression();
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
        if (accept(TokenType::RParen)) {
            return std::make_unique<TupleLiteralNode>();
        }
        auto e = parseOrExpr();
        if (accept(TokenType::Comma)) {
            auto tup = std::make_unique<TupleLiteralNode>();
            tup->elements.push_back(std::move(e));
            while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
                auto next = parseExpression();
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
        auto first = parseOrExpr();
        if (cur_.type == TokenType::For) {
            auto lc = std::make_unique<ListCompNode>();
            lc->elt = std::move(first);
            while (cur_.type == TokenType::For) {
                Comprehension c;
                advance(); // for
                c.target = parseTargetList();
                expect(TokenType::In);
                c.iter = parseOrExpr();
                while (accept(TokenType::If)) {
                    c.ifs.push_back(parseOrExpr());
                }
                lc->generators.push_back(std::move(c));
            }
            expect(TokenType::RSquare);
            return lc;
        }
        auto lst = std::make_unique<ListLiteralNode>();
        if (first) {
            lst->elements.push_back(std::move(first));
            if (accept(TokenType::Comma)) {
                while (cur_.type != TokenType::RSquare && cur_.type != TokenType::EndOfFile) {
                    lst->elements.push_back(parseExpression());
                    if (!accept(TokenType::Comma)) break;
                }
            }
        }
        expect(TokenType::RSquare);
        return lst;
    }
    if (accept(TokenType::LCurly)) {
        auto key = parseExpression();
        if (cur_.type == TokenType::For) {
            auto sc = std::make_unique<SetCompNode>();
            sc->elt = std::move(key);
            while (cur_.type == TokenType::For) {
                Comprehension c;
                advance(); // for
                c.target = parseTargetList();
                expect(TokenType::In);
                c.iter = parseOrExpr();
                while (accept(TokenType::If)) {
                    c.ifs.push_back(parseOrExpr());
                }
                sc->generators.push_back(std::move(c));
            }
            expect(TokenType::RCurly);
            return sc;
        }
        if (accept(TokenType::Colon)) {
            auto val = parseExpression();
            if (cur_.type == TokenType::For) {
                auto dc = std::make_unique<DictCompNode>();
                dc->key = std::move(key);
                dc->value = std::move(val);
                while (cur_.type == TokenType::For) {
                    Comprehension c;
                    advance(); // for
                    c.target = parseTargetList();
                    expect(TokenType::In);
                    c.iter = parseOrExpr();
                    while (accept(TokenType::If)) {
                        c.ifs.push_back(parseOrExpr());
                    }
                    dc->generators.push_back(std::move(c));
                }
                expect(TokenType::RCurly);
                return dc;
            }
            auto d = std::make_unique<DictLiteralNode>();
            d->keys.push_back(std::move(key));
            d->values.push_back(std::move(val));
            if (accept(TokenType::Comma)) {
                while (cur_.type != TokenType::RCurly && cur_.type != TokenType::EndOfFile) {
                    auto k = parseExpression();
                    if (!expect(TokenType::Colon)) return nullptr;
                    auto v = parseExpression();
                    d->keys.push_back(std::move(k));
                    d->values.push_back(std::move(v));
                    if (!accept(TokenType::Comma)) break;
                }
            }
            expect(TokenType::RCurly);
            return d;
        }
        // Set literal or empty dict?
        // {} is empty dict.
        // {x} is set literal.
        // For now, let's just keep original dict literal logic if possible, 
        // but handle the case where key was already parsed.
        auto d = std::make_unique<DictLiteralNode>();
        // If we got here, it's either an empty dict or a set literal (unsupported).
        // Original code:
        /*
        while (cur_.type != TokenType::RCurly && cur_.type != TokenType::EndOfFile) {
            auto k = parseExpression();
            if (!expect(TokenType::Colon)) return nullptr;
            auto v = parseExpression();
            d->keys.push_back(std::move(k));
            d->values.push_back(std::move(v));
            if (!accept(TokenType::Comma)) break;
        }
        */
        // Let's stick to dicts for now.
        if (key) {
            // key exists but no colon? error.
            error("expected ':' in dict literal");
            return nullptr;
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
                    call->keywords.push_back({kwname, parseExpression()});
                } else {
                    if (!call->keywords.empty()) {
                        error("positional argument follows keyword argument");
                    }
                    call->args.push_back(parseExpression());
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
    auto left = parseNotExpr();
    if (!left) return nullptr;
    while (cur_.type == TokenType::And) {
        advance();
        auto right = parseNotExpr();
        if (!right) return left;
        auto bin = std::make_unique<BinOpNode>();
        bin->left = std::move(left);
        bin->op = TokenType::And;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseNotExpr() {
    if (accept(TokenType::Not)) {
        auto operand = parseNotExpr();
        if (!operand) return nullptr;
        auto n = std::make_unique<UnaryOpNode>();
        n->op = TokenType::Not;
        n->operand = std::move(operand);
        return n;
    }
    return parseCompareExpr();
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
    if (cur_.type == TokenType::Assert) {
        advance();
        auto a = std::make_unique<AssertNode>();
        a->test = parseExpression();
        if (accept(TokenType::Comma)) {
            a->msg = parseExpression();
        }
        return a;
    }
    if (cur_.type == TokenType::Del) {
        advance();
        auto d = std::make_unique<DeleteNode>();
        d->targets.push_back(parsePrimary());
        while (accept(TokenType::Comma)) {
            auto t = parsePrimary();
            if (t) d->targets.push_back(std::move(t));
            else break;
        }
        return d;
    }
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
                auto defaultVal = parseExpression();
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
    if (cur_.type == TokenType::Class) {
        advance();
        if (cur_.type != TokenType::Name) return nullptr;
        std::string className = cur_.value;
        advance();
        auto cl = std::make_unique<ClassDefNode>();
        cl->name = className;
        if (accept(TokenType::LParen)) {
            while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
                auto base = parseExpression();
                if (base) cl->bases.push_back(std::move(base));
                if (!accept(TokenType::Comma)) break;
            }
            if (!expect(TokenType::RParen)) return nullptr;
        }
        if (!expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        cl->body = std::move(body);
        return cl;
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
        auto target = parseTargetList();
        if (!target || !expect(TokenType::In)) return nullptr;
        auto iter = parseExpression();
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
        auto test = parseExpression();
        if (!test || !expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        auto iff = std::make_unique<IfNode>();
        iff->test = std::move(test);
        iff->body = std::move(body);
        skipNewlines();
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
            ret->value = parseExpression();
        }
        return ret;
    }
    if (cur_.type == TokenType::Import) {
        advance();
        if (cur_.type != TokenType::Name) return nullptr;
        std::string modName = cur_.value;
        advance();
        while (accept(TokenType::Dot)) {
            if (cur_.type != TokenType::Name) return nullptr;
            modName += ".";
            modName += cur_.value;
            advance();
        }
        auto imp = std::make_unique<ImportNode>();
        imp->moduleName = modName;
        if (accept(TokenType::As)) {
            if (cur_.type != TokenType::Name) return nullptr;
            imp->alias = cur_.value;
            imp->isAs = true;
            advance();
        } else {
            size_t dot = modName.find('.');
            imp->alias = (dot == std::string::npos) ? modName : modName.substr(0, dot);
            imp->isAs = false;
        }
        return imp;
    }
    if (cur_.type == TokenType::Try) {
        advance();
        if (!expect(TokenType::Colon)) return nullptr;
        auto t = std::make_unique<TryNode>();
        t->body = parseSuite();
        skipNewlines();
        while (accept(TokenType::Except)) {
            if (cur_.type == TokenType::Name) {
                advance(); // Simple skip exception type
                if (accept(TokenType::As)) {
                    if (cur_.type == TokenType::Name) advance();
                }
            }
            if (!expect(TokenType::Colon)) return nullptr;
            t->handlers = parseSuite(); // For now just keep the last one or skip
            skipNewlines();
        }
        if (accept(TokenType::Finally)) {
            if (!expect(TokenType::Colon)) return nullptr;
            t->finalbody = parseSuite();
        }
        return t;
    }
    auto expr = parseOrExpr();
    if (!expr) {
        if (hasError_) return nullptr;
        std::string msg = "Unexpected token at statement start: ";
        msg += tokenToName(cur_.type);
        if (!cur_.value.empty()) {
            msg += " ('";
            msg += cur_.value;
            msg += "')";
        }
        error(msg);
        return nullptr;
    }
    if (accept(TokenType::Assign)) {
        auto value = parseExpression();
        if (!value) return nullptr;
        auto a = std::make_unique<AssignNode>();
        a->target = std::move(expr);
        a->value = std::move(value);
        return a;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseExpression() {
    auto node = parseOrExpr();
    if (accept(TokenType::If)) {
        auto c = std::make_unique<CondExprNode>();
        c->body = std::move(node);
        c->cond = parseOrExpr();
        if (expect(TokenType::Else)) {
            c->orelse = parseExpression();
        }
        return c;
    }
    return node;
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
        if (!st) {
            if (!hasError_) error("Failed to parse statement");
            break;
        }
        mod->body.push_back(std::move(st));
        while (accept(TokenType::Semicolon) && cur_.type != TokenType::Newline && cur_.type != TokenType::EndOfFile) {
            auto s2 = parseStatement();
            if (s2) mod->body.push_back(std::move(s2));
            else break;
        }
        skipNewlines();
        if (hasError_) break;
    }
    return mod;
}

std::unique_ptr<ASTNode> Parser::parseTargetList() {
    auto left = parseAddExpr();
    if (cur_.type == TokenType::Comma) {
        auto t = std::make_unique<TupleLiteralNode>();
        t->elements.push_back(std::move(left));
        while (accept(TokenType::Comma)) {
            if (cur_.type == TokenType::In) break;
            t->elements.push_back(parseAddExpr());
        }
        return t;
    }
    return left;
}

} // namespace protoPython
