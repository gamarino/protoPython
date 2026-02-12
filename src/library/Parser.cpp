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
    if (cur_.type != TokenType::Colon) {
        first = parseExpression();
        if (!accept(TokenType::Colon)) {
            expect(TokenType::RSquare);
            return first;
        }
    } else {
        advance(); // Consume the first ':'
    }

    /* Slice: [start:] [stop] [ :step] */
    auto sl = createNode<SliceNode>();
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
        auto n = createNode<ConstantNode>();
        n->constType = cur_.isInteger ? ConstantNode::ConstType::Int : ConstantNode::ConstType::Float;
        n->intVal = cur_.intValue;
        n->floatVal = cur_.numValue;
        advance();
        return n;
    }
    if (cur_.type == TokenType::String) {
        auto n = createNode<ConstantNode>();
        n->constType = ConstantNode::ConstType::Str;
        n->strVal = cur_.value;
        advance();
        return n;
    }
    if (cur_.type == TokenType::FString) {
        return parseFString();
    }
    if (cur_.type == TokenType::Name) {
        auto n = createNode<NameNode>();
        n->id = cur_.value;
        advance();
        return n;
    }
    if (accept(TokenType::LParen)) {
        if (accept(TokenType::RParen)) {
            return createNode<TupleLiteralNode>();
        }
        auto e = parseOrExpr();
        if (cur_.type == TokenType::For) {
            auto ge = createNode<GeneratorExpNode>();
            ge->elt = std::move(e);
            while (cur_.type == TokenType::For) {
                Comprehension c;
                advance(); // for
                c.target = parseTargetList();
                expect(TokenType::In);
                c.iter = parseOrExpr();
                while (accept(TokenType::If)) {
                    c.ifs.push_back(parseOrExpr());
                }
                ge->generators.push_back(std::move(c));
            }
            expect(TokenType::RParen);
            return ge;
        }
        if (accept(TokenType::Comma)) {
            auto tup = createNode<TupleLiteralNode>();
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
        if (accept(TokenType::RSquare)) {
            return createNode<ListLiteralNode>();
        }
        auto first = parseOrExpr();
        if (cur_.type == TokenType::For) {
            auto lc = createNode<ListCompNode>();
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
        auto lst = createNode<ListLiteralNode>();
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
        if (accept(TokenType::RCurly)) {
            return createNode<DictLiteralNode>();
        }
        auto key = parseExpression();
        if (cur_.type == TokenType::For) {
            auto sc = createNode<SetCompNode>();
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
                auto dc = createNode<DictCompNode>();
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
            auto d = createNode<DictLiteralNode>();
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
        auto d = createNode<DictLiteralNode>();
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
        auto n = createNode<ConstantNode>();
        n->constType = ConstantNode::ConstType::Bool;
        n->intVal = (cur_.type == TokenType::True) ? 1 : 0;
        advance();
        return n;
    }
    if (cur_.type == TokenType::None) {
        auto n = createNode<ConstantNode>();
        n->constType = ConstantNode::ConstType::None;
        advance();
        return n;
    }
    if (cur_.type == TokenType::Lambda) {
        return parseLambda();
    }
    
    if (!hasError_) {
        std::string msg = "expected expression, but got ";
        msg += tokenToName(cur_.type);
        if (!cur_.value.empty()) msg += " ('" + cur_.value + "')";
        error(msg);
    }
    return nullptr;
}

std::unique_ptr<ASTNode> Parser::parsePrimary() {
    auto left = parseAtom();
    if (!left) return nullptr;
    for (;;) {
        if (accept(TokenType::Dot)) {
            if (cur_.type != TokenType::Name) return left;
            auto att = createNode<AttributeNode>();
            att->value = std::move(left);
            att->attr = cur_.value;
            advance();
            left = std::move(att);
            continue;
        }
        if (accept(TokenType::LSquare)) {
            auto sub = parseSubscript();
            if (!sub) return left;
            auto subn = createNode<SubscriptNode>();
            subn->value = std::move(left);
            subn->index = std::move(sub);
            left = std::move(subn);
            continue;
        }
        if (cur_.type == TokenType::LParen) {
            advance();
            auto call = createNode<CallNode>();
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
    if (cur_.type == TokenType::Minus || cur_.type == TokenType::Plus || cur_.type == TokenType::Tilde) {
        TokenType op = cur_.type;
        advance();
        auto operand = parseUnary();
        if (!operand) return nullptr;
        auto u = createNode<UnaryOpNode>();
        u->op = op;
        u->operand = std::move(operand);
        return u;
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
        auto bin = createNode<BinOpNode>();
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
        auto bin = createNode<BinOpNode>();
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
            // If it's one of the simple comparison ops we haven't advanced yet
            if (op == TokenType::EqEqual || op == TokenType::NotEqual ||
                op == TokenType::Less || op == TokenType::LessEqual ||
                op == TokenType::Greater || op == TokenType::GreaterEqual) {
                advance();
            }
            auto right = parseAddExpr();
            if (!right) return left;
            auto bin = createNode<BinOpNode>();
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
        auto bin = createNode<BinOpNode>();
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
        auto n = createNode<UnaryOpNode>();
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
        auto bin = createNode<BinOpNode>();
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
        auto a = createNode<AssertNode>();
        a->test = parseExpression();
        if (accept(TokenType::Comma)) {
            a->msg = parseExpression();
        }
        return a;
    }
    if (cur_.type == TokenType::Del) {
        advance();
        auto d = createNode<DeleteNode>();
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
        return createNode<PassNode>();
    }
    if (cur_.type == TokenType::At) {
        std::vector<std::unique_ptr<ASTNode>> decorators;
        while (accept(TokenType::At)) {
            decorators.push_back(parsePrimary());
            skipNewlines();
        }
        if (cur_.type == TokenType::Def) {
            auto node = parseStatement();
            if (auto fn = dynamic_cast<FunctionDefNode*>(node.get())) {
                fn->decorator_list = std::move(decorators);
            }
            return node;
        } else if (cur_.type == TokenType::Class) {
            auto node = parseStatement();
            if (auto cl = dynamic_cast<ClassDefNode*>(node.get())) {
                cl->decorator_list = std::move(decorators);
            }
            return node;
        } else {
            error("expected 'def' or 'class' after decorator");
            return nullptr;
        }
    }
    if (cur_.type == TokenType::Def) {
        advance();
        if (cur_.type != TokenType::Name) return nullptr;
        std::string funcName = cur_.value;
        advance();
        if (!expect(TokenType::LParen)) return nullptr;
        auto fn = createNode<FunctionDefNode>();
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
        auto cl = createNode<ClassDefNode>();
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
        return parseGlobal();
    }
    if (cur_.type == TokenType::Nonlocal) {
        return parseNonlocal();
    }
    if (cur_.type == TokenType::For) {
        advance();
        auto target = parseTargetList();
        if (!target || !expect(TokenType::In)) return nullptr;
        auto iter = parseExpression();
        if (!iter || !expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        auto f = createNode<ForNode>();
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
        auto iff = createNode<IfNode>();
        iff->test = std::move(test);
        iff->body = std::move(body);
        skipNewlines();
        if (accept(TokenType::Else)) {
            if (!expect(TokenType::Colon)) return nullptr;
            iff->orelse = parseSuite();
        }
        return iff;
    }
    if (cur_.type == TokenType::While) {
        advance();
        auto test = parseExpression();
        if (!test || !expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        auto w = createNode<WhileNode>();
        w->test = std::move(test);
        w->body = std::move(body);
        skipNewlines();
        if (accept(TokenType::Else)) {
            if (!expect(TokenType::Colon)) return nullptr;
            w->orelse = parseSuite();
        }
        return w;
    }
    if (cur_.type == TokenType::Return) {
        advance();
        auto ret = createNode<ReturnNode>();
        if (cur_.type != TokenType::Newline && cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile) {
            ret->value = parseExpression();
        }
        return ret;
    }
    if (cur_.type == TokenType::Break) {
        advance();
        return createNode<BreakNode>();
    }
    if (cur_.type == TokenType::Continue) {
        advance();
        return createNode<ContinueNode>();
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
        auto imp = createNode<ImportNode>();
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
    if (cur_.type == TokenType::From) {
        advance();
        auto imp = createNode<ImportFromNode>();
        while (accept(TokenType::Dot)) {
            imp->level++;
        }
        if (cur_.type == TokenType::Name) {
            imp->moduleName = cur_.value;
            advance();
            while (accept(TokenType::Dot)) {
                if (cur_.type != TokenType::Name) return nullptr;
                imp->moduleName += ".";
                imp->moduleName += cur_.value;
                advance();
            }
        }
        if (!expect(TokenType::Import)) return nullptr;
        if (accept(TokenType::Star)) {
             imp->names.push_back({"*", ""});
        } else {
            for (;;) {
                if (cur_.type != TokenType::Name) break;
                std::string name = cur_.value;
                advance();
                std::string alias;
                if (accept(TokenType::As)) {
                    if (cur_.type != TokenType::Name) return nullptr;
                    alias = cur_.value;
                    advance();
                }
                imp->names.push_back({name, alias});
                if (!accept(TokenType::Comma)) break;
            }
        }
        return imp;
    }
    if (cur_.type == TokenType::Try) {
        advance();
        if (!expect(TokenType::Colon)) return nullptr;
        auto t = createNode<TryNode>();
        t->body = parseSuite();
        skipNewlines();
        while (accept(TokenType::Except)) {
            ExceptHandler h;
            if (cur_.type != TokenType::Colon) {
                h.type = parseExpression();
                if (accept(TokenType::As)) {
                    if (cur_.type == TokenType::Name) {
                        h.name = cur_.value;
                        advance();
                    } else {
                        error("expected name after 'as' in except");
                    }
                }
            }
            if (!expect(TokenType::Colon)) return nullptr;
            h.body = parseSuite();
            t->handlers.push_back(std::move(h));
            skipNewlines();
        }
        if (accept(TokenType::Else)) {
            if (!expect(TokenType::Colon)) return nullptr;
            t->orelse = parseSuite();
            skipNewlines();
        }
        if (accept(TokenType::Finally)) {
            if (!expect(TokenType::Colon)) return nullptr;
            t->finalbody = parseSuite();
        }
        return t;
    }
    if (cur_.type == TokenType::Raise) {
        advance();
        auto r = createNode<RaiseNode>();
        if (cur_.type != TokenType::Newline && cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile && cur_.type != TokenType::Semicolon) {
            r->exc = parseExpression();
            if (accept(TokenType::From)) {
                r->cause = parseExpression();
            }
        }
        return r;
    }
    if (cur_.type == TokenType::With) {
        advance();
        auto w = createNode<WithNode>();
        for (;;) {
            WithItem item;
            item.context_expr = parseExpression();
            if (accept(TokenType::As)) {
                item.optional_vars = parseTargetList();
            }
            w->items.push_back(std::move(item));
            if (!accept(TokenType::Comma)) break;
        }
        if (!expect(TokenType::Colon)) return nullptr;
        w->body = parseSuite();
        return w;
    }
    auto expr = parseExpression();
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
        auto a = createNode<AssignNode>();
        a->target = std::move(expr);
        a->value = std::move(value);
        return a;
    }
    if (cur_.type == TokenType::PlusAssign || cur_.type == TokenType::MinusAssign ||
        cur_.type == TokenType::StarAssign || cur_.type == TokenType::SlashAssign) {
        TokenType op = cur_.type;
        advance();
        auto value = parseExpression();
        if (!value) return nullptr;
        auto a = createNode<AugAssignNode>();
        a->target = std::move(expr);
        a->op = op;
        a->value = std::move(value);
        return a;
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parseYieldExpression() {
    advance(); // yield
    auto node = createNode<YieldNode>();
    if (accept(TokenType::From)) {
        node->isFrom = true;
        node->value = parseExpression();
    } else {
        // yield is allowed without value
        if (cur_.type != TokenType::Newline && cur_.type != TokenType::Dedent &&
            cur_.type != TokenType::RParen && cur_.type != TokenType::RSquare &&
            cur_.type != TokenType::RCurly && cur_.type != TokenType::Comma && 
            cur_.type != TokenType::Semicolon && cur_.type != TokenType::EndOfFile) {
            node->value = parseExpression();
        }
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseExpression() {
    if (cur_.type == TokenType::Yield) {
        return parseYieldExpression();
    }
    if (cur_.type == TokenType::Lambda) {
        return parseLambda();
    }
    auto node = parseOrExpr();
    if (accept(TokenType::If)) {
        auto c = createNode<CondExprNode>();
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
    auto suite = createNode<SuiteNode>();
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
        suite->statements.push_back(createNode<PassNode>());
    return suite;
}

std::unique_ptr<ModuleNode> Parser::parseModule() {
    auto mod = createNode<ModuleNode>();
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
        auto t = createNode<TupleLiteralNode>();
        t->elements.push_back(std::move(left));
        while (accept(TokenType::Comma)) {
            if (cur_.type == TokenType::In) break;
            t->elements.push_back(parseAddExpr());
        }
        return t;
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseLambda() {
    auto node = createNode<LambdaNode>();
    advance(); // lambda
    
    if (cur_.type != TokenType::Colon) {
        while (cur_.type == TokenType::Name) {
            node->parameters.push_back(cur_.value);
            advance();
            if (accept(TokenType::Comma)) continue;
            else break;
        }
    }
    
    if (!expect(TokenType::Colon)) return nullptr;
    node->body = parseExpression();
    return node;
}

std::unique_ptr<ASTNode> Parser::parseFString() {
    std::string raw = cur_.value;
    auto joined = createNode<JoinedStrNode>();
    advance(); // FString token
    
    size_t i = 0;
    while (i < raw.size()) {
        size_t nextOpen = raw.find('{', i);
        size_t nextClose = raw.find('}', i);
        
        if (nextOpen == std::string::npos && nextClose == std::string::npos) {
            auto part = createNode<ConstantNode>();
            part->constType = ConstantNode::ConstType::Str;
            part->strVal = raw.substr(i);
            joined->values.push_back(std::move(part));
            break;
        }
        
        size_t next = std::min(nextOpen == std::string::npos ? (size_t)-1 : nextOpen, 
                               nextClose == std::string::npos ? (size_t)-1 : nextClose);
        
        if (next > i) {
            auto part = createNode<ConstantNode>();
            part->constType = ConstantNode::ConstType::Str;
            part->strVal = raw.substr(i, next - i);
            joined->values.push_back(std::move(part));
            i = next;
        }
        
        if (i < raw.size() && raw[i] == '{') {
            if (i + 1 < raw.size() && raw[i + 1] == '{') {
                auto part = createNode<ConstantNode>();
                part->constType = ConstantNode::ConstType::Str;
                part->strVal = "{";
                joined->values.push_back(std::move(part));
                i += 2;
            } else {
                i++;
                size_t closeBrace = raw.find('}', i);
                if (closeBrace == std::string::npos) {
                    error("f-string: missing '}'");
                    return nullptr;
                }
                std::string exprStr = raw.substr(i, closeBrace - i);
                i = closeBrace + 1;
                
                Parser subParser(exprStr);
                auto expr = subParser.parseExpression();
                if (subParser.hasError()) {
                    error("f-string expression error: " + subParser.getLastErrorMsg());
                    return nullptr;
                }
                auto fv = createNode<FormattedValueNode>();
                fv->value = std::move(expr);
                joined->values.push_back(std::move(fv));
            }
        } else if (i < raw.size() && raw[i] == '}') {
            if (i + 1 < raw.size() && raw[i + 1] == '}') {
                auto part = createNode<ConstantNode>();
                part->constType = ConstantNode::ConstType::Str;
                part->strVal = "}";
                joined->values.push_back(std::move(part));
                i += 2;
            } else {
                error("f-string: single '}' is not allowed");
                return nullptr;
            }
        }
    }
    return joined;
}


std::unique_ptr<ASTNode> Parser::parseGlobal() {
    advance(); // global
    auto g = createNode<GlobalNode>();
    if (cur_.type != TokenType::Name) {
        error("global: expected name");
        return nullptr;
    }
    g->names.push_back(cur_.value);
    advance();
    while (accept(TokenType::Comma) && cur_.type == TokenType::Name) {
        g->names.push_back(cur_.value);
        advance();
    }
    return g;
}

std::unique_ptr<ASTNode> Parser::parseNonlocal() {
    advance(); // nonlocal
    auto n = createNode<NonlocalNode>();
    if (cur_.type != TokenType::Name) {
        error("nonlocal: expected name");
        return nullptr;
    }
    n->names.push_back(cur_.value);
    advance();
    while (accept(TokenType::Comma) && cur_.type == TokenType::Name) {
        n->names.push_back(cur_.value);
        advance();
    }
    return n;
}

} // namespace protoPython
