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
        case TokenType::Arrow: return "'->'";
        case TokenType::Walrus: return "':='";
        case TokenType::Greater: return "'>'";
        case TokenType::LessEqual: return "'<='";
        case TokenType::GreaterEqual: return "'>='";
        case TokenType::Yield: return "'yield'";
        case TokenType::Pass: return "'pass'";
        case TokenType::Indent: return "indent";
        case TokenType::Dedent: return "dedent";
        case TokenType::Semicolon: return "';'";
        case TokenType::DoubleStar: return "'**'";
        case TokenType::BitAnd: return "'&'";
        case TokenType::BitOr: return "'|'";
        case TokenType::BitXor: return "'^'";
        case TokenType::LShift: return "'<<'";
        case TokenType::RShift: return "'>>'";
        case TokenType::Async: return "'async'";
        case TokenType::Await: return "'await'";
        case TokenType::DoubleSlash: return "'//'";
        case TokenType::DoubleSlashAssign: return "'//='";
        case TokenType::At: return "'@'";
        case TokenType::AtAssign: return "'@='";
        case TokenType::ModuloAssign: return "'%='";
        case TokenType::DoubleStarAssign: return "'**='";
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

bool Parser::isName(TokenType t) {
    return t == TokenType::Name || t == TokenType::Type || t == TokenType::Match || t == TokenType::Case;
}

bool Parser::acceptName() {
    if (isName(cur_.type)) {
        advance();
        return true;
    }
    return false;
}

// Recursively walk an expression AST looking for a YieldNode.  Used to
// implement CPython's "'yield' inside list comprehension" SyntaxError —
// a yield is illegal anywhere inside a comp body (elt, predicate, inner
// iter), even though it is allowed in the OUTERMOST iterable (which
// runs in the enclosing scope).  We don't need to recurse through
// nested comprehensions or lambdas here: if a nested comp has its own
// yield, the inner parsePrimary call already validated it; if it's
// inside a lambda, the lambda body is its own scope and the yield
// would have been rejected when the lambda was parsed.
static bool containsYield(const ASTNode* n) {
    if (!n) return false;
    if (dynamic_cast<const YieldNode*>(n)) return true;
    if (auto* b = dynamic_cast<const BinOpNode*>(n)) {
        return containsYield(b->left.get()) || containsYield(b->right.get());
    }
    if (auto* u = dynamic_cast<const UnaryOpNode*>(n)) {
        return containsYield(u->operand.get());
    }
    if (auto* c = dynamic_cast<const CondExprNode*>(n)) {
        return containsYield(c->body.get()) || containsYield(c->cond.get())
            || containsYield(c->orelse.get());
    }
    if (auto* c = dynamic_cast<const ConditionalExprNode*>(n)) {
        return containsYield(c->body.get()) || containsYield(c->test.get())
            || containsYield(c->orelse.get());
    }
    if (auto* s = dynamic_cast<const StarredNode*>(n)) {
        return containsYield(s->value.get());
    }
    if (auto* call = dynamic_cast<const CallNode*>(n)) {
        if (containsYield(call->func.get())) return true;
        for (auto& a : call->args) if (containsYield(a.get())) return true;
        for (auto& kv : call->keywords) if (containsYield(kv.second.get())) return true;
        return false;
    }
    if (auto* a = dynamic_cast<const AttributeNode*>(n)) {
        return containsYield(a->value.get());
    }
    if (auto* sub = dynamic_cast<const SubscriptNode*>(n)) {
        return containsYield(sub->value.get()) || containsYield(sub->index.get());
    }
    if (auto* sl = dynamic_cast<const SliceNode*>(n)) {
        return containsYield(sl->start.get()) || containsYield(sl->stop.get())
            || containsYield(sl->step.get());
    }
    if (auto* lst = dynamic_cast<const ListLiteralNode*>(n)) {
        for (auto& e : lst->elements) if (containsYield(e.get())) return true;
        return false;
    }
    if (auto* d = dynamic_cast<const DictLiteralNode*>(n)) {
        for (auto& k : d->keys) if (containsYield(k.get())) return true;
        for (auto& v : d->values) if (containsYield(v.get())) return true;
        return false;
    }
    if (auto* s = dynamic_cast<const SetLiteralNode*>(n)) {
        for (auto& e : s->elements) if (containsYield(e.get())) return true;
        return false;
    }
    if (auto* t = dynamic_cast<const TupleLiteralNode*>(n)) {
        for (auto& e : t->elements) if (containsYield(e.get())) return true;
        return false;
    }
    if (auto* j = dynamic_cast<const JoinedStrNode*>(n)) {
        for (auto& v : j->values) if (containsYield(v.get())) return true;
        return false;
    }
    if (auto* f = dynamic_cast<const FormattedValueNode*>(n)) {
        return containsYield(f->value.get());
    }
    if (auto* ne = dynamic_cast<const NamedExprNode*>(n)) {
        return containsYield(ne->target.get()) || containsYield(ne->value.get());
    }
    return false;
}

// True if any expression in the comp body contains a yield.  Outermost
// iterable (generators[0].iter) is intentionally excluded — that is
// evaluated in the enclosing scope and yield there is legal.
static bool comprehensionBodyContainsYield(const ASTNode* elt,
                                            const ASTNode* secondElt,
                                            const std::vector<Comprehension>& gens) {
    if (containsYield(elt)) return true;
    if (containsYield(secondElt)) return true;
    for (size_t i = 0; i < gens.size(); ++i) {
        // Skip outermost iterable (i == 0 for the first generator's iter
        // is evaluated in the enclosing scope).
        if (i > 0 && containsYield(gens[i].iter.get())) return true;
        for (auto& cond : gens[i].ifs) if (containsYield(cond.get())) return true;
    }
    return false;
}

bool Parser::expect(TokenType t) {
    if (cur_.type == t) {
        advance();
        return true;
    }
    // CPython's tokenizer phrases "unclosed bracket" errors as
    // "'(' was never closed" / "'[' was never closed" / "'{' was never
    // closed".  Emit that phrasing when we expected a closing bracket
    // and found EOF.
    if (cur_.type == TokenType::EndOfFile &&
        (t == TokenType::RParen || t == TokenType::RSquare || t == TokenType::RCurly)) {
        const char* opener =
            t == TokenType::RParen ? "(" :
            t == TokenType::RSquare ? "[" : "{";
        std::string msg = std::string("'") + opener + "' was never closed";
        error(msg);
        return false;
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

void Parser::skipTrash() {
    while (cur_.type == TokenType::Newline || cur_.type == TokenType::Indent || cur_.type == TokenType::Dedent)
        advance();
}

std::unique_ptr<ASTNode> Parser::parseSubscript() {
    auto parsePart = [&]() -> std::unique_ptr<ASTNode> {
        if (cur_.type == TokenType::RSquare) return nullptr;
        if (cur_.type == TokenType::Colon) {
             advance();
             auto sl = createNode<SliceNode>();
             if (cur_.type != TokenType::Colon && cur_.type != TokenType::RSquare && cur_.type != TokenType::Comma) {
                 sl->stop = parseExpression();
             }
             if (accept(TokenType::Colon) && cur_.type != TokenType::RSquare && cur_.type != TokenType::Comma) {
                 sl->step = parseExpression();
             }
             return sl;
        }
        auto first = parseExpression();
        if (accept(TokenType::Colon)) {
             auto sl = createNode<SliceNode>();
             sl->start = std::move(first);
             if (cur_.type != TokenType::Colon && cur_.type != TokenType::RSquare && cur_.type != TokenType::Comma) {
                 sl->stop = parseExpression();
             }
             if (accept(TokenType::Colon) && cur_.type != TokenType::RSquare && cur_.type != TokenType::Comma) {
                 sl->step = parseExpression();
             }
             return sl;
        }
        return first;
    };

    std::vector<std::unique_ptr<ASTNode>> parts;
    auto p = parsePart();
    if (p) parts.push_back(std::move(p));

    bool sawComma = false;
    while (accept(TokenType::Comma)) {
        sawComma = true;
        if (cur_.type == TokenType::RSquare) break;
        auto p2 = parsePart();
        if (p2) parts.push_back(std::move(p2));
    }
    expect(TokenType::RSquare);

    // `d[x]` is a single subscript; `d[x,]` is `d[(x,)]` (a 1-tuple),
    // matching CPython.  A trailing comma after a lone expression must
    // therefore still produce a tuple.
    if (parts.size() == 1 && !sawComma) return std::move(parts[0]);

    auto t = createNode<TupleLiteralNode>();
    t->elements = std::move(parts);
    return t;
}

std::unique_ptr<ASTNode> Parser::parseAtom() {
    if (cur_.type == TokenType::Number) {
        auto n = createNode<ConstantNode>();
        n->constType = cur_.isImaginary
            ? ConstantNode::ConstType::Imaginary
            : (cur_.isInteger ? ConstantNode::ConstType::Int : ConstantNode::ConstType::Float);
        n->intVal = cur_.intValue;
        n->floatVal = cur_.numValue;
        // Propagate bignum payload for literals that overflow int64.
        if (!cur_.bigDigits.empty()) {
            n->bigIntDigits = cur_.bigDigits;
            n->bigBase = cur_.bigBase;
        }
        // Propagate the lexer's "invalid <kind> literal" SyntaxWarning
        // hint so the compiler can emit it at this node's source line.
        if (!cur_.pendingWarning.empty()) {
            n->pendingWarning = cur_.pendingWarning;
        }
        advance();
        return n;
    }
    if (cur_.type == TokenType::Await) {
        advance();
        auto a = createNode<AwaitNode>();
        a->value = parsePrimary(); 
        return a;
    }
    if (cur_.type == TokenType::String || cur_.type == TokenType::FString || cur_.type == TokenType::Bytes) {
        std::vector<std::unique_ptr<ASTNode>> allValues;
        bool anyF = false;
        bool anyT = false;     // PEP 750 t-string anywhere in the concat
        bool anyB = cur_.type == TokenType::Bytes;

        while (cur_.type == TokenType::String || cur_.type == TokenType::FString || cur_.type == TokenType::Bytes) {
            if (cur_.type == TokenType::String || cur_.type == TokenType::Bytes) {
                auto n = createNode<ConstantNode>();
                n->constType = (cur_.type == TokenType::Bytes) ? ConstantNode::ConstType::Bytes : ConstantNode::ConstType::Str;
                if (cur_.type == TokenType::Bytes) n->bytesVal = cur_.value;
                else n->strVal = cur_.value;
                allValues.push_back(std::move(n));
                advance();
            } else {
                anyF = true;
                if (cur_.isTString) anyT = true;
                auto fs = parseFString();
                if (!fs) return nullptr;
                if (auto* jsn = dynamic_cast<JoinedStrNode*>(fs.get())) {
                    if (jsn->isTString) anyT = true;
                    for (auto& v : jsn->values) {
                        allValues.push_back(std::move(v));
                    }
                } else {
                    allValues.push_back(std::move(fs));
                }
            }
        }

        if (!anyF) {
            std::string finalStr;
            std::string finalBytes;
            for (auto& v : allValues) {
                if (auto* cn = dynamic_cast<ConstantNode*>(v.get())) {
                    if (cn->constType == ConstantNode::ConstType::Bytes) finalBytes += cn->bytesVal;
                    else finalStr += cn->strVal;
                }
            }
            auto n = createNode<ConstantNode>();
            if (anyB) {
                n->constType = ConstantNode::ConstType::Bytes;
                n->bytesVal = finalBytes;
            } else {
                n->constType = ConstantNode::ConstType::Str;
                n->strVal = finalStr;
            }
            return n;
        } else {
            auto res = createNode<JoinedStrNode>();
            res->values = std::move(allValues);
            res->isTString = anyT;   // propagate PEP 750 flag
            return res;
        }
    }
    if (cur_.type == TokenType::Name || cur_.type == TokenType::Type || cur_.type == TokenType::Match || cur_.type == TokenType::Case) {
        auto n = createNode<NameNode>();
        n->id = cur_.value;
        advance();
        return n;
    }
    if (accept(TokenType::LParen)) {
        // CPython caps expression nesting at MAXLEVEL=200 and raises
        // SyntaxError("too many nested parentheses") beyond that.
        // Use a simple static counter tied to the parser's lifetime —
        // a recursive parseAtom never unwinds the counter otherwise.
        static thread_local int parenDepth = 0;
        const int MAXLEVEL = 200;
        parenDepth++;
        struct ParenGuard { int& d; ~ParenGuard(){ --d; } } _pg{parenDepth};
        if (parenDepth > MAXLEVEL) {
            error("too many nested parentheses");
            return nullptr;
        }
        skipTrash();
        if (accept(TokenType::RParen)) {
            return createNode<TupleLiteralNode>();
        }

        if (cur_.type == TokenType::Yield) {
            auto y = parseYieldExpression();
            if (!expect(TokenType::RParen)) return nullptr;
            // Mark the yield as parenthesized so subsequent parseTestList
            // / call-arg parsers can detect a *bare* yield in disallowed
            // tuple-comma contexts without flagging legal `(yield)` forms.
            if (auto* yn = dynamic_cast<YieldNode*>(y.get())) {
                yn->parenthesized = true;
            }
            return y;
        }
        
        std::unique_ptr<ASTNode> e;
        bool isStarred = false;
        if (cur_.type == TokenType::Star) {
            advance();
            auto star = createNode<StarredNode>();
            star->value = parseOrExpr();
            e = std::move(star);
            isStarred = true;
        } else {
            e = parseExpression();
        }

        bool isComp = cur_.type == TokenType::For || (cur_.type == TokenType::Async && tok_.peek().type == TokenType::For);
        if (!isStarred && isComp) {
            auto ge = createNode<GeneratorExpNode>();
            ge->elt = std::move(e);
            ge->generators = parseComprehensions();
            expect(TokenType::RParen);
            if (comprehensionBodyContainsYield(ge->elt.get(), nullptr, ge->generators)) {
                error("'yield' inside generator expression");
                return ge;
            }
            return ge;
        }
        if (isStarred || accept(TokenType::Comma)) {
            auto tup = createNode<TupleLiteralNode>();
            tup->elements.push_back(std::move(e));
            if (!isStarred) {
                // We already accepted the comma
                skipTrash();
            } else {
                // If it was starred, we MUST have a comma for it to be a tuple if there's only one element?
                // Actually (*l) is a syntax error in Python. (*l,) is a tuple.
                // For simplicity, if it's starred, we'll try to parse it as a tuple.
                if (!accept(TokenType::Comma)) {
                    // Python doesn't allow (*l)
                    // We can either error or just allow it as a "starred expression" in parens (which is weird)
                    // Let's stick to CPython: it must have a comma or more elements.
                }
                skipTrash();
            }

            while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
                if (cur_.type == TokenType::Star) {
                    advance();
                    auto star = createNode<StarredNode>();
                    star->value = parseOrExpr();
                    tup->elements.push_back(std::move(star));
                } else {
                    tup->elements.push_back(parseExpression());
                }
                skipTrash();
                if (!accept(TokenType::Comma)) break;
                skipTrash();
            }
            expect(TokenType::RParen);
            return tup;
        }
        expect(TokenType::RParen);
        // Mark comparison nodes as parenthesized so the compiler won't chain them
        if (auto* bin = dynamic_cast<BinOpNode*>(e.get())) {
            bin->parenthesized = true;
        }
        // Mark a parenthesized name so AnnAssign targets like `(x): int`
        // can be detected — CPython treats those as no-ops for local
        // binding, while `x: int` declares x as a local.
        if (auto* nm = dynamic_cast<NameNode*>(e.get())) {
            nm->parenthesized = true;
        }
        return e;
    }
    if (accept(TokenType::LSquare)) {
        skipTrash();
        if (accept(TokenType::RSquare)) {
            return createNode<ListLiteralNode>();
        }
        
        std::unique_ptr<ASTNode> first;
        bool isStarred = false;
        if (cur_.type == TokenType::Star) {
            advance();
            auto star = createNode<StarredNode>();
            star->value = parseOrExpr();
            first = std::move(star);
            isStarred = true;
        } else {
            first = parseExpression();
        }

        bool isComp = cur_.type == TokenType::For || (cur_.type == TokenType::Async && tok_.peek().type == TokenType::For);
        if (!isStarred && isComp) {
            auto lc = createNode<ListCompNode>();
            lc->elt = std::move(first);
            lc->generators = parseComprehensions();
            expect(TokenType::RSquare);
            if (comprehensionBodyContainsYield(lc->elt.get(), nullptr, lc->generators)) {
                error("'yield' inside list comprehension");
                return lc;
            }
            return lc;
        }
        auto lst = createNode<ListLiteralNode>();
        if (first) {
            lst->elements.push_back(std::move(first));
            skipTrash();
            while (accept(TokenType::Comma)) {
                skipTrash();
                if (cur_.type == TokenType::RSquare) break;
                if (cur_.type == TokenType::Star) {
                    advance();
                    auto star = createNode<StarredNode>();
                    star->value = parseOrExpr();
                    lst->elements.push_back(std::move(star));
                } else {
                    lst->elements.push_back(parseExpression());
                }
                skipTrash();
            }
        }
        expect(TokenType::RSquare);
        return lst;
    }
    if (accept(TokenType::LCurly)) {
        skipTrash();
        if (accept(TokenType::RCurly)) {
            return createNode<DictLiteralNode>();
        }
        
        std::unique_ptr<ASTNode> firstKey;
        bool isStarred = false;
        bool isDoubleStarred = false;
        
        if (cur_.type == TokenType::DoubleStar) {
            advance();
            auto dstar = createNode<StarredNode>();
            dstar->value = parseOrExpr();
            firstKey = std::move(dstar);
            isDoubleStarred = true;
        } else if (cur_.type == TokenType::Star) {
            advance();
            auto star = createNode<StarredNode>();
            star->value = parseOrExpr();
            firstKey = std::move(star);
            isStarred = true;
        } else {
            firstKey = parseExpression();
        }

        bool isComp = cur_.type == TokenType::For || (cur_.type == TokenType::Async && tok_.peek().type == TokenType::For);
        if (!isStarred && !isDoubleStarred && isComp) {
            auto sc = createNode<SetCompNode>();
            sc->elt = std::move(firstKey);
            sc->generators = parseComprehensions();
            expect(TokenType::RCurly);
            if (comprehensionBodyContainsYield(sc->elt.get(), nullptr, sc->generators)) {
                error("'yield' inside set comprehension");
                return sc;
            }
            return sc;
        }
        
        if (isDoubleStarred || accept(TokenType::Colon)) {
            // Dict literal
            auto d = createNode<DictLiteralNode>();
            if (isDoubleStarred) {
                d->keys.push_back(nullptr);
                d->values.push_back(std::move(firstKey));
            } else {
                auto val = parseExpression();
                bool isComp = cur_.type == TokenType::For || (cur_.type == TokenType::Async && tok_.peek().type == TokenType::For);
                if (isComp) {
                    auto dc = createNode<DictCompNode>();
                    dc->key = std::move(firstKey);
                    dc->value = std::move(val);
                    dc->generators = parseComprehensions();
                    expect(TokenType::RCurly);
                    if (comprehensionBodyContainsYield(dc->key.get(), dc->value.get(), dc->generators)) {
                        error("'yield' inside dict comprehension");
                        return dc;
                    }
                    return dc;
                }
                d->keys.push_back(std::move(firstKey));
                d->values.push_back(std::move(val));
            }

            while (accept(TokenType::Comma)) {
                skipTrash();
                if (cur_.type == TokenType::RCurly) break;
                if (cur_.type == TokenType::DoubleStar) {
                    advance();
                    auto star = createNode<StarredNode>();
                    star->value = parseOrExpr();
                    d->keys.push_back(nullptr);
                    d->values.push_back(std::move(star));
                } else {
                    auto k = parseExpression();
                    if (!expect(TokenType::Colon)) return nullptr;
                    skipTrash();
                    auto v = parseExpression();
                    d->keys.push_back(std::move(k));
                    d->values.push_back(std::move(v));
                }
                skipTrash();
            }
            expect(TokenType::RCurly);
            return d;
        }
        
        // Set literal
        auto s = createNode<SetLiteralNode>();
        s->elements.push_back(std::move(firstKey));
        while (accept(TokenType::Comma)) {
            skipTrash();
            if (cur_.type == TokenType::RCurly) break;
            if (cur_.type == TokenType::Star) {
                advance();
                auto star = createNode<StarredNode>();
                star->value = parseOrExpr();
                s->elements.push_back(std::move(star));
            } else {
                s->elements.push_back(parseExpression());
            }
            skipTrash();
        }
        expect(TokenType::RCurly);
        return s;
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
    if (cur_.type == TokenType::Ellipsis) {
        auto n = createNode<ConstantNode>();
        n->constType = ConstantNode::ConstType::Ellipsis;
        n->strVal = "..."; 
        advance();
        return n;
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
            if (cur_.type != TokenType::Name && cur_.type != TokenType::Type && cur_.type != TokenType::Match && cur_.type != TokenType::Case) {
                error("invalid syntax");
                return nullptr;
            }
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
            skipTrash();
            auto call = createNode<CallNode>();
            call->func = std::move(left);
            while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
                skipTrash();
                if (cur_.type == TokenType::Star) {
                    advance();
                    auto star = createNode<StarredNode>();
                    star->value = parseExpression();
                    call->args.push_back(std::move(star));
                } else if (cur_.type == TokenType::DoubleStar) {
                    advance();
                    call->keywords.push_back({"", parseExpression()});
                } else if (isName(cur_.type) && tok_.peek().type == TokenType::Assign) {
                    std::string kwname = cur_.value;
                    advance(); // name
                    advance(); // =
                    // Reject duplicate keyword arguments at parse time —
                    // `f(x=1, x=2)` and `f(x=1, *(2,3), x=5)` are both
                    // SyntaxError in CPython.  Catching this here means
                    // we don't need to detect it at call time, and it
                    // matches the message test.support.check_syntax_error
                    // looks for via test_grammar.test_funcdef.
                    for (const auto& kw : call->keywords) {
                        if (!kw.first.empty() && kw.first == kwname) {
                            error("keyword argument repeated: " + kwname);
                            return left;
                        }
                    }
                    call->keywords.push_back({kwname, parseExpression()});
                } else {
                    auto arg = parseExpression();
                    // Bare `yield` is only allowed in expression statements
                    // and as the RHS of assignment.  Inside a call-argument
                    // list, `f(yield 1)` is SyntaxError; `f((yield 1))` is
                    // fine.  We detect by checking whether parseExpression
                    // returned a YieldNode whose `parenthesized` flag is
                    // false.
                    if (auto* yn = dynamic_cast<YieldNode*>(arg.get())) {
                        if (!yn->parenthesized) {
                            error("yield expression must be parenthesized when used as a call argument");
                            return left;
                        }
                    }
                    bool isComp = cur_.type == TokenType::For || (cur_.type == TokenType::Async && tok_.peek().type == TokenType::For);
                    if (call->args.empty() && call->keywords.empty() && isComp) {
                        // list(x for x in y)
                        auto ge = createNode<GeneratorExpNode>();
                        ge->elt = std::move(arg);
                        ge->generators = parseComprehensions();
                        if (comprehensionBodyContainsYield(ge->elt.get(), nullptr, ge->generators)) {
                            error("'yield' inside generator expression");
                            return left;
                        }
                        call->args.push_back(std::move(ge));
                        // CPython: a bare generator expression must be the
                        // sole positional argument.  Once it's parsed, the
                        // call's argument list is closed — anything other
                        // than the matching `)` is a SyntaxError, including
                        // `foo(x for x in y, 100)`.
                        if (cur_.type == TokenType::Comma) {
                            error("Generator expression must be parenthesized if not sole argument");
                            return left;
                        }
                    } else if (isComp) {
                        // `foo(100, x for x in range(10))` — the bare genexp
                        // appears after another argument.  Same rule: must
                        // be parenthesized.
                        error("Generator expression must be parenthesized if not sole argument");
                        return left;
                    } else {
                        call->args.push_back(std::move(arg));
                    }
                }
                if (!accept(TokenType::Comma)) break;
                skipTrash();
            }
            expect(TokenType::RParen);
            left = std::move(call);
            continue;
        }
        break;
    }
    return left;
}


std::unique_ptr<ASTNode> Parser::parsePower() {
    auto left = parsePrimary();
    if (!left) return nullptr;
    if (accept(TokenType::DoubleStar)) {
        auto right = parseUnary();
        if (!right) return left;
        auto bin = createNode<BinOpNode>();
        bin->left = std::move(left);
        bin->op = TokenType::DoubleStar;
        bin->right = std::move(right);
        return bin;
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
    return parsePower();
}

std::unique_ptr<ASTNode> Parser::parseMulExpr() {
    auto left = parseUnary();
    if (!left) return nullptr;
    while (cur_.type == TokenType::Star || cur_.type == TokenType::Slash || cur_.type == TokenType::Modulo || cur_.type == TokenType::DoubleSlash || cur_.type == TokenType::At) {
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
    auto left = parseBitOr();
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
                op = TokenType::IsNot;
                advance();
            }
        } else if (op == TokenType::In) {
            matched = true;
            advance();
        } else if (op == TokenType::Not) {
            Token nextToken = tok_.peek();
            if (nextToken.type == TokenType::In) {
                matched = true;
                advance(); // not
                advance(); // in
                op = TokenType::NotIn;
            }
        }
        
        if (matched) {
            if (op == TokenType::EqEqual || op == TokenType::NotEqual ||
                op == TokenType::Less || op == TokenType::LessEqual ||
                op == TokenType::Greater || op == TokenType::GreaterEqual) {
                advance();
            }
            auto right = parseBitOr();
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

std::unique_ptr<ASTNode> Parser::parseBitOr() {
    auto left = parseBitXor();
    if (!left) return nullptr;
    while (cur_.type == TokenType::BitOr) {
        advance();
        auto right = parseBitXor();
        if (!right) return left;
        auto bin = createNode<BinOpNode>();
        bin->left = std::move(left);
        bin->op = TokenType::BitOr;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseBitXor() {
    auto left = parseBitAnd();
    if (!left) return nullptr;
    while (cur_.type == TokenType::BitXor) {
        advance();
        auto right = parseBitAnd();
        if (!right) return left;
        auto bin = createNode<BinOpNode>();
        bin->left = std::move(left);
        bin->op = TokenType::BitXor;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseBitAnd() {
    auto left = parseShiftExpr();
    if (!left) return nullptr;
    while (cur_.type == TokenType::BitAnd) {
        advance();
        auto right = parseShiftExpr();
        if (!right) return left;
        auto bin = createNode<BinOpNode>();
        bin->left = std::move(left);
        bin->op = TokenType::BitAnd;
        bin->right = std::move(right);
        left = std::move(bin);
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseShiftExpr() {
    auto left = parseAddExpr();
    if (!left) return nullptr;
    while (cur_.type == TokenType::LShift || cur_.type == TokenType::RShift) {
        TokenType op = cur_.type;
        advance();
        auto right = parseAddExpr();
        if (!right) return left;
        auto bin = createNode<BinOpNode>();
        bin->left = std::move(left);
        bin->op = op;
        bin->right = std::move(right);
        left = std::move(bin);
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

bool Parser::parseParameters(FunctionDefNode* fn) {
    if (!expect(TokenType::LParen)) return false;
    bool isKwOnly = false;
    while (cur_.type == TokenType::Name || cur_.type == TokenType::Type || cur_.type == TokenType::Match || cur_.type == TokenType::Case || cur_.type == TokenType::Star || cur_.type == TokenType::DoubleStar || cur_.type == TokenType::Slash) {
        // printf("DEBUG: parseParameters loop cur=%s val=%s\n", tokenToName(cur_.type), cur_.value.c_str());
        if (cur_.type == TokenType::Slash) {
            advance();
            fn->posonlyargcount = static_cast<int>(fn->parameters.size());
        } else if (cur_.type == TokenType::Star) {
            advance();
            if (cur_.type == TokenType::Name || cur_.type == TokenType::Type || cur_.type == TokenType::Match || cur_.type == TokenType::Case) {
                fn->vararg = cur_.value;
                std::string varName = fn->vararg;
                advance();
                if (accept(TokenType::Colon)) {
                    auto ann = parseExpression();
                    if (containsYield(ann.get())) {
                        error("'yield' outside function");
                        return false;
                    }
                    fn->parameter_annotations[varName] = std::move(ann);
                }
                isKwOnly = true;
            } else {
                isKwOnly = true;
            }
        } else if (cur_.type == TokenType::DoubleStar) {
            advance();
            if (cur_.type != TokenType::Name && cur_.type != TokenType::Type && cur_.type != TokenType::Match && cur_.type != TokenType::Case) {
                error("Expected name after **");
                return false;
            }
            fn->kwarg = cur_.value;
            std::string kwName = fn->kwarg;
            advance();
            if (accept(TokenType::Colon)) {
                auto ann = parseExpression();
                if (containsYield(ann.get())) {
                    error("'yield' outside function");
                    return false;
                }
                fn->parameter_annotations[kwName] = std::move(ann);
            }
        } else if (cur_.type == TokenType::Name || cur_.type == TokenType::Type || cur_.type == TokenType::Match || cur_.type == TokenType::Case) {
            std::string paramName = cur_.value;
            advance();
            if (accept(TokenType::Colon)) {
                auto ann = parseExpression();
                if (containsYield(ann.get())) {
                    error("'yield' outside function");
                    return false;
                }
                fn->parameter_annotations[paramName] = std::move(ann);
            }
            if (isKwOnly) {
                fn->kwonlyargs.push_back(paramName);
                if (accept(TokenType::Assign)) {
                    fn->kw_defaults.push_back(parseExpression());
                } else {
                    fn->kw_defaults.push_back(nullptr);
                }
            } else {
                fn->parameters.push_back(paramName);
                if (accept(TokenType::Assign)) {
                    fn->defaults.push_back(parseExpression());
                }
            }
        }
        if (!accept(TokenType::Comma)) break;
        skipTrash();
    }
    return expect(TokenType::RParen);
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    skipNewlines();
    if (cur_.type == TokenType::Assert) return parseAssert();
    if (cur_.type == TokenType::Del) return parseDelete();
    if (cur_.type == TokenType::Pass) { advance(); return createNode<PassNode>(); }
    if (cur_.type == TokenType::Type && tok_.peek().type == TokenType::Name) return parseTypeAlias();
    if (cur_.type == TokenType::Match && tok_.peek().type != TokenType::Assign && 
        tok_.peek().type != TokenType::Dot && tok_.peek().type != TokenType::LSquare &&
        tok_.peek().type != TokenType::PlusAssign && tok_.peek().type != TokenType::MinusAssign) 
        return parseMatch();
    if (cur_.type == TokenType::At) {
        std::vector<std::unique_ptr<ASTNode>> decorators;
        while (accept(TokenType::At)) {
            decorators.push_back(parseExpression());
            skipNewlines();
        }
        if (cur_.type == TokenType::Def || cur_.type == TokenType::Async) {
            auto node = parseStatement();
            if (auto fn = dynamic_cast<FunctionDefNode*>(node.get())) {
                fn->decorator_list = std::move(decorators);
            } else if (auto afn = dynamic_cast<AsyncFunctionDefNode*>(node.get())) {
                afn->decorator_list = std::move(decorators);
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
    if (cur_.type == TokenType::Def) return parseFunctionDef();
    if (cur_.type == TokenType::Class) return parseClassDef();
    if (cur_.type == TokenType::Async) return parseAsync();

    if (cur_.type == TokenType::Return) return parseReturn();
    if (cur_.type == TokenType::Raise) return parseRaise();
    if (cur_.type == TokenType::With) return parseWith();
    if (cur_.type == TokenType::If) return parseIf();
    if (cur_.type == TokenType::While) return parseWhile();
    if (cur_.type == TokenType::For) return parseFor();
    if (cur_.type == TokenType::Try) return parseTry();
    if (cur_.type == TokenType::Import) return parseImport();
    if (cur_.type == TokenType::From) return parseImportFrom();
    if (cur_.type == TokenType::Global) return parseGlobal();
    if (cur_.type == TokenType::Nonlocal) return parseNonlocal();
    if (cur_.type == TokenType::Break) { advance(); return createNode<BreakNode>(); }
    if (cur_.type == TokenType::Continue) { advance(); return createNode<ContinueNode>(); }
    if (cur_.type == TokenType::Async) return parseAsync();
    auto expr = parseTestList();
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
    if (accept(TokenType::Colon)) {
        auto annotation = parseExpression();
        auto a = createNode<AnnAssignNode>();
        a->target = std::move(expr);
        a->annotation = std::move(annotation);
        if (accept(TokenType::Assign)) {
            // `x: T = 1, 2` is valid and the RHS is a tuple expression,
            // matching CPython behaviour.  parseTestList handles both the
            // single-value and the comma-separated tuple case.
            a->value = parseTestList();
        }
        return a;
    }
    if (cur_.type == TokenType::Assign) {
        auto a = createNode<AssignNode>();
        a->targets.push_back(std::move(expr));
        while (accept(TokenType::Assign)) {
            auto val = parseTestList();
            if (cur_.type == TokenType::Assign) {
                a->targets.push_back(std::move(val));
            } else {
                a->value = std::move(val);
                return a;
            }
        }
        error("expected expression after '='");
        return nullptr;
    }
    if (cur_.type == TokenType::PlusAssign || cur_.type == TokenType::MinusAssign ||
        cur_.type == TokenType::StarAssign || cur_.type == TokenType::SlashAssign ||
        cur_.type == TokenType::ModuloAssign || cur_.type == TokenType::AndAssign ||
        cur_.type == TokenType::OrAssign || cur_.type == TokenType::XorAssign ||
        cur_.type == TokenType::LShiftAssign || cur_.type == TokenType::RShiftAssign ||
        cur_.type == TokenType::DoubleStarAssign || cur_.type == TokenType::DoubleSlashAssign ||
        cur_.type == TokenType::AtAssign) {
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
    // After a bare expression statement, the only legal followers are
    // newline/semicolon/EOF/dedent/closing paren.  Anything else is the
    // Python-2-style "print foo" / "exec foo" construct (or generally a
    // leftover identifier).  Emit a targeted SyntaxError matching
    // CPython's wording so `assertRaisesRegex(SyntaxError, "call to
    // 'print'")` succeeds.
    if (cur_.type != TokenType::Newline && cur_.type != TokenType::Semicolon &&
        cur_.type != TokenType::EndOfFile && cur_.type != TokenType::Dedent &&
        cur_.type != TokenType::RParen && cur_.type != TokenType::RSquare &&
        cur_.type != TokenType::RCurly && cur_.type != TokenType::Comma) {
        if (auto* nm = dynamic_cast<NameNode*>(expr.get())) {
            if (nm->id == "print" || nm->id == "exec") {
                // Python-2-style `print EXPR` / `exec EXPR` produces the
                // CPython-style hint, but only when EXPR itself is a
                // syntactically valid expression — otherwise the inner
                // error wins ("invalid syntax").  We try to parse the
                // remainder as a testlist; if it succeeds without error,
                // emit the hint.  If it fails, the inner error has
                // already been recorded and propagates.
                int savedLine = cur_.line;
                int savedCol = cur_.column;
                bool savedHasError = hasError_;
                std::string savedErrMsg = lastErrorMsg_;
                int savedErrLine = lastErrorLine_;
                int savedErrCol = lastErrorColumn_;
                auto trail = parseTestList();
                if (!hasError_ && trail) {
                    // The remainder parsed cleanly — emit the Python-2 hint.
                    hasError_ = savedHasError;
                    lastErrorMsg_ = savedErrMsg;
                    lastErrorLine_ = savedErrLine;
                    lastErrorColumn_ = savedErrCol;
                    (void)savedLine; (void)savedCol;
                    std::string msg = "Missing parentheses in call to '";
                    msg += nm->id;
                    msg += "'. Did you mean " + nm->id + "(...)?";
                    error(msg);
                    return nullptr;
                }
                // Inner parse failed — let its error propagate.
                return nullptr;
            }
        }
        error("invalid syntax");
        return nullptr;
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
        // yield is allowed without value; yield x, y yields the tuple (x, y)
        if (cur_.type != TokenType::Newline && cur_.type != TokenType::Dedent &&
            cur_.type != TokenType::RParen && cur_.type != TokenType::RSquare &&
            cur_.type != TokenType::RCurly &&
            cur_.type != TokenType::Semicolon && cur_.type != TokenType::EndOfFile) {
            node->value = parseTestList();
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
    if (cur_.type == TokenType::Walrus) {
        advance();
        auto value = parseExpression();
        auto ne = createNode<NamedExprNode>();
        ne->target = std::move(node);
        ne->value = std::move(value);
        return ne;
    }
    if (accept(TokenType::If)) {
        auto c = createNode<ConditionalExprNode>();
        c->body = std::move(node);
        c->test = parseOrExpr();
        if (expect(TokenType::Else)) {
            c->orelse = parseExpression();
        }
        return c;
    }
    return node;
}

std::unique_ptr<ASTNode> Parser::parseTestList() {
    std::unique_ptr<ASTNode> left;
    if (cur_.type == TokenType::Star) {
        advance();
        auto sn = createNode<StarredNode>();
        sn->value = parseOrExpr();
        left = std::move(sn);
    } else {
        left = parseExpression();
    }

    if (cur_.type == TokenType::Comma) {
        // Bare `yield` may not mix with comma-tuple at top level —
        // `1, yield 1` is SyntaxError in CPython.  Parenthesized
        // `1, (yield 1)` is fine.
        if (auto* yn = dynamic_cast<YieldNode*>(left.get())) {
            if (!yn->parenthesized) {
                error("yield expression must be parenthesized when used in a comma-separated expression");
                return left;
            }
        }
        auto t = createNode<TupleLiteralNode>();
        t->elements.push_back(std::move(left));
        while (accept(TokenType::Comma)) {
            if (cur_.type == TokenType::Colon || cur_.type == TokenType::Newline ||
                cur_.type == TokenType::Dedent || cur_.type == TokenType::EndOfFile ||
                cur_.type == TokenType::RParen || cur_.type == TokenType::RCurly || cur_.type == TokenType::RSquare ||
                cur_.type == TokenType::Assign || cur_.type == TokenType::Semicolon) break;

            std::unique_ptr<ASTNode> e;
            if (cur_.type == TokenType::Star) {
                advance();
                auto sn = createNode<StarredNode>();
                sn->value = parseOrExpr();
                e = std::move(sn);
            } else {
                e = parseExpression();
                // Bare `yield` as a non-first element is also disallowed.
                if (auto* yn = dynamic_cast<YieldNode*>(e.get())) {
                    if (!yn->parenthesized) {
                        error("yield expression must be parenthesized when used in a comma-separated expression");
                        return left;
                    }
                }
            }
            if (e) t->elements.push_back(std::move(e));
            else break;
        }
        return t;
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseSuite() {
    skipNewlines();
    auto suite = createNode<SuiteNode>();
    if (accept(TokenType::Indent)) {
        while (cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile) {
            bool comp = isCompound(cur_.type);
            auto st = parseStatement();
            if (st) suite->statements.push_back(std::move(st));
            else if (!hasError_) {
                error(std::string("Unexpected token in suite: ") + tokenToName(cur_.type));
                advance();
            }
            if (hasError_) break;
            if (!comp) {
                while (accept(TokenType::Semicolon) && cur_.type != TokenType::Newline && cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile) {
                    auto s2 = parseStatement();
                    if (s2) suite->statements.push_back(std::move(s2));
                    else break;
                }
            }
            skipNewlines();
        }
        if (!expect(TokenType::Dedent))
            return nullptr;
    } else {
        bool first = true;
        while (cur_.type != TokenType::Newline && cur_.type != TokenType::EndOfFile) {
            if (isCompound(cur_.type)) {
                if (!first) {
                    error("compound statement not allowed in statement list");
                    return nullptr;
                }
                auto st = parseStatement();
                if (st) suite->statements.push_back(std::move(st));
                // After a compound statement on a single line, we expect NEWLINE or EOF
                if (cur_.type != TokenType::Newline && cur_.type != TokenType::EndOfFile && cur_.type != TokenType::Semicolon) {
                    // CPython allows nothing after the compound statement's suite on same line
                }
                if (cur_.type == TokenType::Semicolon) {
                    error("semicolon not allowed after compound statement");
                    return nullptr;
                }
                break; // Compound statement ends the line
            }
            auto st = parseStatement();
            if (st) suite->statements.push_back(std::move(st));
            first = false;
            if (!accept(TokenType::Semicolon)) break;
        }
    }
    if (suite->statements.empty())
        suite->statements.push_back(createNode<PassNode>());
    return suite;
}

std::unique_ptr<ModuleNode> Parser::parseModule() {
    auto mod = createNode<ModuleNode>();
    skipNewlines();
    while (cur_.type != TokenType::EndOfFile) {
        bool comp = isCompound(cur_.type);
        auto st = parseStatement();
        if (st) mod->body.push_back(std::move(st));

        if (!comp) {
            while (accept(TokenType::Semicolon)) {
                if (cur_.type == TokenType::Newline || cur_.type == TokenType::EndOfFile) break;
                if (isCompound(cur_.type)) {
                    error("compound statement not allowed after semicolon");
                    break;
                }
                auto s2 = parseStatement();
                if (s2) mod->body.push_back(std::move(s2));
                else break;
            }
        } else {
            // After compound statement, semicolon is NOT allowed on same line
            if (cur_.type == TokenType::Semicolon) {
                error("semicolon not allowed after compound statement");
                advance();
            }
        }
        skipNewlines();
        if (hasError_) break;
    }
    return mod;
}

std::unique_ptr<ASTNode> Parser::parseTargetList() {
    // Helper to parse one target element, supporting *name (PEP 3132 starred targets)
    auto parseOneTarget = [&]() -> std::unique_ptr<ASTNode> {
        if (cur_.type == TokenType::Star) {
            advance();
            auto sn = createNode<StarredNode>();
            sn->value = parseAddExpr();
            return sn;
        }
        return parseAddExpr();
    };

    auto left = parseOneTarget();
    if (cur_.type == TokenType::Comma) {
        auto t = createNode<TupleLiteralNode>();
        t->elements.push_back(std::move(left));
        while (accept(TokenType::Comma)) {
            if (cur_.type == TokenType::In) break;
            t->elements.push_back(parseOneTarget());
        }
        return t;
    }
    return left;
}

std::unique_ptr<ASTNode> Parser::parseLambda() {
    auto node = createNode<LambdaNode>();
    advance(); // lambda

    // Lambdas use parseParameters but without the name and often without parens in some grammars,
    // but in Python they are: lambda [parameters]: expression
    // Our parseParameters expects (.
    // Let's implement a simpler version for lambda or make parseParameters more flexible.
    if (cur_.type != TokenType::Colon) {
        // Simple parameter parsing for lambda
        bool isKwOnly = false;
        while (cur_.type == TokenType::Name || cur_.type == TokenType::Type || cur_.type == TokenType::Match || cur_.type == TokenType::Case || cur_.type == TokenType::Star || cur_.type == TokenType::DoubleStar) {
            if (cur_.type == TokenType::Star) {
                advance();
                if (cur_.type == TokenType::Name || cur_.type == TokenType::Type || cur_.type == TokenType::Match || cur_.type == TokenType::Case) {
                    node->vararg = cur_.value;
                    advance();
                    isKwOnly = true;
                } else {
                    isKwOnly = true;
                }
            } else if (cur_.type == TokenType::DoubleStar) {
                advance();
                node->kwarg = cur_.value;
                advance();
            } else {
                if (isKwOnly) {
                    node->kwonlyargs.push_back(cur_.value);
                    advance();
                    if (accept(TokenType::Assign)) {
                        node->kw_defaults.push_back(parseExpression());
                    } else {
                        node->kw_defaults.push_back(nullptr);
                    }
                } else {
                    node->parameters.push_back(cur_.value);
                    advance();
                    if (accept(TokenType::Assign)) {
                        node->defaults.push_back(parseExpression());
                    }
                }
            }
            if (!accept(TokenType::Comma)) break;
        }
    }
    if (!expect(TokenType::Colon)) return nullptr;
    node->body = parseExpression();
    return node;
}

std::unique_ptr<ASTNode> Parser::parseFString() {
    std::string raw = cur_.value;
    auto joined = createNode<JoinedStrNode>();
    // Propagate the lexer's t-prefix flag so downstream stages
    // (missed-comma analyzer, runtime type reporting) can distinguish
    // PEP 750 t-strings from PEP 498 f-strings.  Both share the same
    // interpolation AST but are reported as distinct runtime types.
    joined->isTString = cur_.isTString;
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
                size_t closeBrace = std::string::npos;
                int braceDepth = 1;
                for (size_t searchIdx = i; searchIdx < raw.size(); ++searchIdx) {
                    if (raw[searchIdx] == '{') {
                        braceDepth++;
                    } else if (raw[searchIdx] == '}') {
                        braceDepth--;
                        if (braceDepth == 0) {
                            closeBrace = searchIdx;
                            break;
                        }
                    }
                }
                
                if (closeBrace == std::string::npos) {
                    error("f-string: missing '}'");
                    return nullptr;
                }
                std::string fullExprStr = raw.substr(i, closeBrace - i);
                i = closeBrace + 1;
                
                size_t exclamation = std::string::npos;
                size_t colon = std::string::npos;
                int depth = 0;
                for (size_t k = 0; k < fullExprStr.size(); ++k) {
                    if (fullExprStr[k] == '(' || fullExprStr[k] == '[' || fullExprStr[k] == '{') depth++;
                    else if (fullExprStr[k] == ')' || fullExprStr[k] == ']' || fullExprStr[k] == '}') depth--;
                    else if (depth == 0) {
                        if (fullExprStr[k] == '!' && exclamation == std::string::npos) {
                            exclamation = k;
                        } else if (fullExprStr[k] == ':' && colon == std::string::npos) {
                            colon = k;
                            break;
                        }
                    }
                }

                std::string exprStr = fullExprStr;
                char conversion = 0;
                std::string format_spec;

                if (colon != std::string::npos) {
                    format_spec = fullExprStr.substr(colon + 1);
                    exprStr = fullExprStr.substr(0, colon);
                }
                if (exclamation != std::string::npos && (colon == std::string::npos || exclamation < colon)) {
                    std::string convStr = fullExprStr.substr(exclamation + 1, (colon == std::string::npos ? std::string::npos : colon - exclamation - 1));
                    if (!convStr.empty()) conversion = convStr[0];
                    exprStr = fullExprStr.substr(0, exclamation);
                }

                Parser subParser(exprStr);
                auto expr = subParser.parseExpression();
                if (subParser.hasError()) {
                    error("f-string expression error: " + subParser.getLastErrorMsg());
                    return nullptr;
                }
                auto fv = createNode<FormattedValueNode>();
                fv->value = std::move(expr);
                fv->conversion = conversion;
                fv->format_spec = format_spec;
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
    if (!isName(cur_.type)) {
        error("global: expected name");
        return nullptr;
    }
    g->names.push_back(cur_.value);
    advance();
    while (accept(TokenType::Comma) && isName(cur_.type)) {
        g->names.push_back(cur_.value);
        advance();
    }
    return g;
}

std::unique_ptr<ASTNode> Parser::parseNonlocal() {
    advance(); // nonlocal
    auto n = createNode<NonlocalNode>();
    if (!isName(cur_.type)) {
        error("nonlocal: expected name");
        return nullptr;
    }
    n->names.push_back(cur_.value);
    advance();
    while (accept(TokenType::Comma) && isName(cur_.type)) {
        n->names.push_back(cur_.value);
        advance();
    }
    return n;
}


std::unique_ptr<ASTNode> Parser::parseReturn() {
    advance(); // return
    if (cur_.type == TokenType::Newline || cur_.type == TokenType::Semicolon || cur_.type == TokenType::EndOfFile || cur_.type == TokenType::Dedent) {
        return createNode<ReturnNode>();
    }
    auto ret = createNode<ReturnNode>();
    ret->value = parseTestList();
    return ret;
}

bool Parser::isCompound(TokenType t) {
    return t == TokenType::Def || t == TokenType::Class || t == TokenType::For ||
           t == TokenType::Async || t == TokenType::If || t == TokenType::Elif || t == TokenType::Else ||
           t == TokenType::While || t == TokenType::Try || t == TokenType::With || t == TokenType::Match;
}



std::unique_ptr<ASTNode> Parser::parseAssert() {
    advance();
    auto a = createNode<AssertNode>();
    a->test = parseExpression();
    if (accept(TokenType::Comma)) {
        a->msg = parseExpression();
    }
    return a;
}

std::unique_ptr<ASTNode> Parser::parseDelete() {
    advance();
    auto d = createNode<DeleteNode>();
    d->targets.push_back(parsePrimary());
    while (accept(TokenType::Comma)) {
        if (cur_.type == TokenType::Newline || cur_.type == TokenType::Semicolon || cur_.type == TokenType::EndOfFile) break;
        auto t = parsePrimary();
        if (t) d->targets.push_back(std::move(t));
        else break;
    }
    return d;
}

std::unique_ptr<ASTNode> Parser::parseIf() {
    advance();
    auto test = parseExpression();
    if (!test || !expect(TokenType::Colon)) return nullptr;
    auto body = parseSuite();
    auto i = createNode<IfNode>();
    i->test = std::move(test);
    i->body = std::move(body);
    
    IfNode* currentIf = i.get();
    for (;;) {
        skipNewlines();
        if (cur_.type == TokenType::Elif) {
            advance();
            auto elifTest = parseExpression();
            if (!elifTest || !expect(TokenType::Colon)) return nullptr;
            auto elifBody = parseSuite();
            auto elifNode = createNode<IfNode>();
            elifNode->test = std::move(elifTest);
            elifNode->body = std::move(elifBody);
            currentIf->orelse = std::move(elifNode);
            currentIf = static_cast<IfNode*>(currentIf->orelse.get());
        } else if (cur_.type == TokenType::Else) {
            advance();
            skipTrash(); 
            if (cur_.type == TokenType::If) {
                 currentIf->orelse = parseStatement();
            } else {
                 if (cur_.type == TokenType::Colon) advance();
                 else expect(TokenType::Colon);
                 currentIf->orelse = parseSuite(); 
            }
            break;
        } else {
            break;
        }
    }
    return i;
}

std::unique_ptr<ASTNode> Parser::parseWhile() {
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

std::unique_ptr<ASTNode> Parser::parseFor() {
    advance();
    auto target = parseTargetList();
    if (!target || !expect(TokenType::In)) return nullptr;
    auto iter = parseTestList();
    if (!iter || !expect(TokenType::Colon)) return nullptr;
    auto body = parseSuite();
    if (!body) return nullptr;
    auto f = createNode<ForNode>();
    f->target = std::move(target);
    f->iter = std::move(iter);
    f->body = std::move(body);
    skipNewlines();
    if (accept(TokenType::Else)) {
        if (!expect(TokenType::Colon)) return nullptr;
        f->orelse = parseSuite();
    }
    return f;
}

std::unique_ptr<ASTNode> Parser::parseTry() {
    advance();
    if (!expect(TokenType::Colon)) return nullptr;
    auto t = createNode<TryNode>();
    t->body = parseSuite();
    skipNewlines();
    while (cur_.type == TokenType::Except) {
        advance();
        ExceptHandler h;
        if (accept(TokenType::Star)) {
            h.isStar = true;
        }
        if (cur_.type != TokenType::Colon) {
            auto expr = parseExpression();
            if (accept(TokenType::Comma)) {
                auto tup = createNode<TupleLiteralNode>();
                tup->elements.push_back(std::move(expr));
                do {
                    tup->elements.push_back(parseExpression());
                } while (accept(TokenType::Comma));
                h.type = std::move(tup);
            } else {
                h.type = std::move(expr);
            }
            
            if (accept(TokenType::As)) {
                if (isName(cur_.type)) {
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

std::unique_ptr<ASTNode> Parser::parseRaise() {
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

std::unique_ptr<ASTNode> Parser::parseWith() {
    advance();
    auto w = createNode<WithNode>();
    bool parenthesized = accept(TokenType::LParen);
    for (;;) {
        WithItem item;
        item.context_expr = parseExpression();
        if (accept(TokenType::As)) {
            item.optional_vars = parsePrimary();
        }
        w->items.push_back(std::move(item));
        if (!accept(TokenType::Comma)) break;
        if (parenthesized && cur_.type == TokenType::RParen) break;
    }
    if (parenthesized) {
        expect(TokenType::RParen);
    }
    if (!expect(TokenType::Colon)) return nullptr;
    w->body = parseSuite();
    return w;
}

std::unique_ptr<ASTNode> Parser::parseImport() {
    advance();
    auto s = createNode<SuiteNode>();
    for (;;) {
        if (!isName(cur_.type)) break;
        std::string modName = cur_.value;
        advance();
        while (accept(TokenType::Dot)) {
            if (!isName(cur_.type)) break;
            modName += ".";
            modName += cur_.value;
            advance();
        }
        auto imp = createNode<ImportNode>();
        imp->moduleName = modName;
        if (accept(TokenType::As)) {
            if (!isName(cur_.type)) {
                error("expected name after 'as' in import");
                return nullptr;
            }
            imp->alias = cur_.value;
            imp->isAs = true;
            advance();
        } else {
            size_t dot = modName.find('.');
            imp->alias = (dot == std::string::npos) ? modName : modName.substr(0, dot);
            imp->isAs = false;
        }
        s->statements.push_back(std::move(imp));
        if (!accept(TokenType::Comma)) break;
        skipTrash();
    }
    if (s->statements.size() == 1) return std::move(s->statements[0]);
    return s;
}

std::unique_ptr<ASTNode> Parser::parseImportFrom() {
    advance();
    auto imp = createNode<ImportFromNode>();
    while (accept(TokenType::Dot)) {
        imp->level++;
    }
    if (isName(cur_.type)) {
        imp->moduleName = cur_.value;
        advance();
        while (accept(TokenType::Dot)) {
            if (!isName(cur_.type)) {
                error("expected module name after dot");
                return nullptr;
            }
            imp->moduleName += ".";
            imp->moduleName += cur_.value;
            advance();
        }
    }
    if (!expect(TokenType::Import)) return nullptr;
    if (accept(TokenType::Star)) {
        imp->names.push_back({"*", ""});
    } else {
        bool parenthesized = accept(TokenType::LParen);
        for (;;) {
            skipTrash();
            skipNewlines();
            if (!isName(cur_.type)) break;
            std::string name = cur_.value;
            advance();
            std::string alias;
            if (accept(TokenType::As)) {
                if (!isName(cur_.type)) {
                    error("expected alias name after 'as'");
                    return nullptr;
                }
                alias = cur_.value;
                advance();
            }
            imp->names.push_back({name, alias});
            if (!accept(TokenType::Comma)) break;
        }
        if (parenthesized) {
            expect(TokenType::RParen);
        }
    }
    return imp;
}

std::unique_ptr<ASTNode> Parser::parseFunctionDef() {
    advance(); // def
    if (!isName(cur_.type)) {
        error("expected name after 'def'");
        return nullptr;
    }
    std::string funcName = cur_.value;
    advance();
    auto fn = createNode<FunctionDefNode>();
    fn->name = funcName;
    fn->type_params = parseTypeParams();
    if (!parseParameters(fn.get())) return nullptr;
    if (accept(TokenType::Arrow)) {
        auto ret = parseExpression();
        if (containsYield(ret.get())) {
            error("'yield' outside function");
            return nullptr;
        }
        fn->returns = std::move(ret);
    }
    if (cur_.type != TokenType::Colon) return nullptr;
    advance();
    auto body = parseSuite();
    if (!body) return nullptr;
    fn->body = std::move(body);
    return fn;
}

std::unique_ptr<ASTNode> Parser::parseClassDef() {
    advance(); // class
    if (!isName(cur_.type)) return nullptr;
    std::string className = cur_.value;
    advance();
    auto cl = createNode<ClassDefNode>();
    cl->name = className;
    cl->type_params = parseTypeParams();
    if (accept(TokenType::LParen)) {
        while (cur_.type != TokenType::RParen && cur_.type != TokenType::EndOfFile) {
            skipTrash();
            if (isName(cur_.type) && tok_.peek().type == TokenType::Assign) {
                std::string kwname = cur_.value;
                advance(); // name
                advance(); // =
                auto val = parseExpression();
                if (val) cl->keywords.push_back({kwname, std::move(val)});
            } else {
                auto base = parseExpression();
                if (base) cl->bases.push_back(std::move(base));
            }
            if (!accept(TokenType::Comma)) break;
            skipTrash();
        }
        if (!expect(TokenType::RParen)) return nullptr;
    }
    if (!expect(TokenType::Colon)) return nullptr;
    auto body = parseSuite();
    if (!body) return nullptr;
    cl->body = std::move(body);
    return cl;
}

std::unique_ptr<ASTNode> Parser::parseAsync() {
    advance(); // async
    if (cur_.type == TokenType::Def) {
        advance();
        if (!isName(cur_.type)) {
            error("expected name after 'async def'");
            return nullptr;
        }
        std::string funcName = cur_.value;
        advance();
        auto tmpFn = createNode<FunctionDefNode>();
        tmpFn->type_params = parseTypeParams();
        if (!parseParameters(tmpFn.get())) return nullptr;
        if (accept(TokenType::Arrow)) {
            tmpFn->returns = parseExpression();
        }
        if (!expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        
        auto fn = createNode<AsyncFunctionDefNode>();
        fn->name = funcName;
        fn->type_params = std::move(tmpFn->type_params);
        fn->returns = std::move(tmpFn->returns);
        fn->parameters = std::move(tmpFn->parameters);
        fn->kwonlyargs = std::move(tmpFn->kwonlyargs);
        fn->defaults = std::move(tmpFn->defaults);
        fn->kw_defaults = std::move(tmpFn->kw_defaults);
        fn->vararg = std::move(tmpFn->vararg);
        fn->kwarg = std::move(tmpFn->kwarg);
        fn->posonlyargcount = tmpFn->posonlyargcount;
        fn->parameter_annotations = std::move(tmpFn->parameter_annotations);
        fn->body = std::move(body);
        return fn;
    } else if (cur_.type == TokenType::With) {
         advance();
         auto w = createNode<AsyncWithNode>();
         bool parenthesized = accept(TokenType::LParen);
         for (;;) {
             auto ctx = parseExpression();
             std::unique_ptr<ASTNode> vars;
             if (accept(TokenType::As)) {
                 vars = parsePrimary(); 
             }
             w->items.push_back({std::move(ctx), std::move(vars)});
             if (!accept(TokenType::Comma)) break;
             if (parenthesized && cur_.type == TokenType::RParen) break;
         }
         if (parenthesized) {
             expect(TokenType::RParen);
         }
         if (!expect(TokenType::Colon)) return nullptr;
         w->body = parseSuite();
         return w;
    } else if (cur_.type == TokenType::For) {
         advance();
         auto target = parseTargetList();
         if (!target || !expect(TokenType::In)) return nullptr;
         auto iter = parseExpression();
         if (!iter || !expect(TokenType::Colon)) return nullptr;
         auto body = parseSuite();
         
         auto f = createNode<AsyncForNode>();
         f->target = std::move(target);
         f->iter = std::move(iter);
         f->body = std::move(body);
         skipNewlines();
         if (accept(TokenType::Else)) {
             if (!expect(TokenType::Colon)) return nullptr;
             f->orelse = parseSuite();
         }
         return f;
    }
    error("Expected def, with, or for after async");
    return nullptr;
}

std::vector<Comprehension> Parser::parseComprehensions() {
    std::vector<Comprehension> generators;
    for (;;) {
        bool isAsync = accept(TokenType::Async);
        if (cur_.type != TokenType::For) {
            if (isAsync) error("expected 'for' after 'async'");
            break;
        }
        Comprehension c;
        c.is_async = isAsync;
        advance(); // for
        c.target = parseTargetList();
        if (!expect(TokenType::In)) break;
        c.iter = parseOrExpr();
        while (accept(TokenType::If)) {
            c.ifs.push_back(parseOrExpr());
        }
        generators.push_back(std::move(c));
    }
    return generators;
}


std::vector<std::unique_ptr<TypeParamNode>> Parser::parseTypeParams() {
    std::vector<std::unique_ptr<TypeParamNode>> params;
    if (accept(TokenType::LSquare)) {
        while (cur_.type != TokenType::RSquare && cur_.type != TokenType::EndOfFile) {
            auto param = createNode<TypeParamNode>();
            if (accept(TokenType::Star)) {
                if (accept(TokenType::Star)) {
                    param->kind = TypeParamNode::Kind::ParamSpec;
                } else {
                    param->kind = TypeParamNode::Kind::TypeVarTuple;
                }
            } else {
                param->kind = TypeParamNode::Kind::TypeVar;
            }
            if (!isName(cur_.type)) {
                error("expected name in type parameters");
                return {};
            }
            param->name = cur_.value;
            advance();
            
            if (accept(TokenType::Colon)) {
                param->bound = parseExpression();
            } else if (accept(TokenType::Assign)) {
                param->default_val = parseExpression();
            }
            
            params.push_back(std::move(param));
            if (!accept(TokenType::Comma)) break;
        }
        expect(TokenType::RSquare);
    }
    return params;
}

std::unique_ptr<ASTNode> Parser::parseTypeAlias() {
    advance(); // type
    if (!isName(cur_.type)) {
        error("expected name after 'type'");
        return nullptr;
    }
    std::string name = cur_.value;
    advance();
    auto n = createNode<TypeAliasNode>();
    n->name = name;
    n->type_params = parseTypeParams();
    if (!expect(TokenType::Assign)) return nullptr;
    n->value = parseExpression();
    return n;
}

// ----- match/case (PEP 634) parser ---------------------------------------
//
// Subject:    parseTestList() — supports the `match a, b:` tuple shorthand.
// Pattern:    closed-pattern with optional `|` alternation and `as` binding.
// Sub-forms supported: literal, signed-literal, string, bytes, None/True/False,
//                      dotted-attribute value pattern, capture, wildcard `_`,
//                      sequence `[...]`/`(...)`, mapping `{...}`, class pattern
//                      `Cls(args, kw=pat)`, group `(pat)`, or-pattern, as-pattern.

static bool isPatternLiteralStart(TokenType t) {
    return t == TokenType::Number
        || t == TokenType::String
        || t == TokenType::Bytes
        || t == TokenType::FString
        || t == TokenType::True
        || t == TokenType::False
        || t == TokenType::None
        || t == TokenType::Minus
        || t == TokenType::Plus;
}

std::unique_ptr<ASTNode> Parser::parsePatternValueExpr() {
    // Numeric literal (optionally signed), string/bytes/fstring, or dotted name.
    if (cur_.type == TokenType::Minus || cur_.type == TokenType::Plus) {
        TokenType sign = cur_.type;
        advance();
        if (cur_.type != TokenType::Number) {
            error("expected number after sign in pattern literal");
            return nullptr;
        }
        auto inner = parseAtom();
        if (!inner) return nullptr;
        if (sign == TokenType::Minus) {
            auto u = createNode<UnaryOpNode>();
            u->op = TokenType::Minus;
            u->operand = std::move(inner);
            return u;
        }
        return inner;
    }
    if (cur_.type == TokenType::Number || cur_.type == TokenType::String
        || cur_.type == TokenType::Bytes || cur_.type == TokenType::FString) {
        return parseAtom();
    }
    if (isName(cur_.type)) {
        // Dotted attribute chain: name(.name)+ — at least one dot is required
        // for a value pattern. A bare name is a capture, handled by the caller.
        auto first = createNode<NameNode>();
        first->id = cur_.value;
        advance();
        if (cur_.type != TokenType::Dot) {
            // Caller must distinguish capture vs class-pattern; signal by
            // returning the bare NameNode.
            return first;
        }
        std::unique_ptr<ASTNode> node = std::move(first);
        while (cur_.type == TokenType::Dot) {
            advance();
            if (!isName(cur_.type)) {
                error("expected attribute name after '.' in pattern");
                return nullptr;
            }
            auto attr = createNode<AttributeNode>();
            attr->value = std::move(node);
            attr->attr = cur_.value;
            advance();
            node = std::move(attr);
        }
        return node;
    }
    return nullptr;
}

std::unique_ptr<MatchPatternNode> Parser::parseClosedPattern() {
    // Singletons: None / True / False
    if (cur_.type == TokenType::None) {
        advance();
        auto n = createNode<MatchSingletonPatternNode>();
        n->kind = MatchSingletonPatternNode::Kind::None_;
        return n;
    }
    if (cur_.type == TokenType::True) {
        advance();
        auto n = createNode<MatchSingletonPatternNode>();
        n->kind = MatchSingletonPatternNode::Kind::True_;
        return n;
    }
    if (cur_.type == TokenType::False) {
        advance();
        auto n = createNode<MatchSingletonPatternNode>();
        n->kind = MatchSingletonPatternNode::Kind::False_;
        return n;
    }

    // Sequence pattern: [...]
    if (cur_.type == TokenType::LSquare) {
        advance();
        return parseSequencePatternBody(TokenType::RSquare);
    }

    // Mapping pattern: {...}
    if (cur_.type == TokenType::LCurly) {
        advance();
        return parseMappingPatternBody();
    }

    // Group or tuple-sequence pattern: (...)
    if (cur_.type == TokenType::LParen) {
        advance();
        // Empty parens → empty sequence
        if (cur_.type == TokenType::RParen) {
            advance();
            return createNode<MatchSequencePatternNode>();
        }
        auto first = parsePattern();
        if (!first) return nullptr;
        if (cur_.type == TokenType::Comma) {
            // Tuple-style sequence pattern
            auto seq = createNode<MatchSequencePatternNode>();
            seq->patterns.push_back(std::move(first));
            while (cur_.type == TokenType::Comma) {
                advance();
                if (cur_.type == TokenType::RParen) break;
                auto p = parsePattern();
                if (!p) return nullptr;
                seq->patterns.push_back(std::move(p));
            }
            if (!expect(TokenType::RParen)) return nullptr;
            return seq;
        }
        if (!expect(TokenType::RParen)) return nullptr;
        return first; // group
    }

    // Numeric / signed-numeric / string / bytes / fstring literal
    if (cur_.type == TokenType::Number || cur_.type == TokenType::String
        || cur_.type == TokenType::Bytes || cur_.type == TokenType::FString
        || cur_.type == TokenType::Minus || cur_.type == TokenType::Plus) {
        auto v = parsePatternValueExpr();
        if (!v) return nullptr;
        auto n = createNode<MatchValuePatternNode>();
        n->value = std::move(v);
        return n;
    }

    // Name-led: dotted-value pattern, class pattern, capture, or wildcard
    if (isName(cur_.type)) {
        std::string firstName = cur_.value;
        // Wildcard
        if (firstName == "_") {
            advance();
            auto w = createNode<MatchAsPatternNode>();
            w->name = "_";
            return w;
        }
        // Read as dotted-or-bare; parsePatternValueExpr returns NameNode for
        // a bare name and AttributeNode for dotted.
        auto expr = parsePatternValueExpr();
        if (!expr) return nullptr;
        // Class pattern: <expr>(...)
        if (cur_.type == TokenType::LParen) {
            auto cls = createNode<MatchClassPatternNode>();
            cls->cls = std::move(expr);
            advance();
            if (!parseClassPatternArgs(cls.get())) return nullptr;
            return cls;
        }
        // Bare name → capture
        if (auto* nm = dynamic_cast<NameNode*>(expr.get())) {
            auto cap = createNode<MatchAsPatternNode>();
            cap->name = nm->id;
            return cap;
        }
        // Dotted attribute → value pattern
        auto v = createNode<MatchValuePatternNode>();
        v->value = std::move(expr);
        return v;
    }

    error(std::string("expected pattern, got ") + tokenToName(cur_.type));
    return nullptr;
}

std::unique_ptr<MatchPatternNode> Parser::parseSequencePatternBody(TokenType closer) {
    auto seq = createNode<MatchSequencePatternNode>();
    if (cur_.type == closer) {
        advance();
        return seq;
    }
    while (true) {
        if (cur_.type == TokenType::Star) {
            advance();
            auto star = createNode<MatchStarPatternNode>();
            if (isName(cur_.type)) {
                star->name = (cur_.value == "_") ? "" : cur_.value;
                advance();
            } else {
                error("expected name or '_' after '*' in sequence pattern");
                return nullptr;
            }
            seq->patterns.push_back(std::move(star));
        } else {
            auto p = parsePattern();
            if (!p) return nullptr;
            seq->patterns.push_back(std::move(p));
        }
        if (cur_.type == TokenType::Comma) {
            advance();
            if (cur_.type == closer) break;
            continue;
        }
        break;
    }
    if (!expect(closer)) return nullptr;
    return seq;
}

std::unique_ptr<MatchPatternNode> Parser::parseMappingPatternBody() {
    auto m = createNode<MatchMappingPatternNode>();
    if (cur_.type == TokenType::RCurly) {
        advance();
        return m;
    }
    while (true) {
        if (cur_.type == TokenType::DoubleStar) {
            advance();
            if (!isName(cur_.type)) {
                error("expected name after '**' in mapping pattern");
                return nullptr;
            }
            m->rest = cur_.value;
            advance();
            // **rest must be the last entry
            if (cur_.type == TokenType::Comma) advance();
            break;
        }
        // Key: literal or dotted name
        std::unique_ptr<ASTNode> key;
        if (cur_.type == TokenType::Number || cur_.type == TokenType::String
            || cur_.type == TokenType::Bytes || cur_.type == TokenType::FString
            || cur_.type == TokenType::Minus || cur_.type == TokenType::Plus
            || cur_.type == TokenType::True || cur_.type == TokenType::False
            || cur_.type == TokenType::None) {
            // For singletons, use the literal token directly via parseAtom
            if (cur_.type == TokenType::True || cur_.type == TokenType::False
                || cur_.type == TokenType::None) {
                key = parseAtom();
            } else {
                key = parsePatternValueExpr();
            }
        } else if (isName(cur_.type)) {
            // Must be dotted (mapping key pattern restriction)
            key = parsePatternValueExpr();
            if (key && dynamic_cast<NameNode*>(key.get())) {
                error("mapping pattern key must be literal or attribute");
                return nullptr;
            }
        } else {
            error("expected key in mapping pattern");
            return nullptr;
        }
        if (!key) return nullptr;
        if (!expect(TokenType::Colon)) return nullptr;
        auto v = parsePattern();
        if (!v) return nullptr;
        m->keys.push_back(std::move(key));
        m->patterns.push_back(std::move(v));
        if (cur_.type == TokenType::Comma) {
            advance();
            if (cur_.type == TokenType::RCurly) break;
            continue;
        }
        break;
    }
    if (!expect(TokenType::RCurly)) return nullptr;
    return m;
}

bool Parser::parseClassPatternArgs(MatchClassPatternNode* dst) {
    if (cur_.type == TokenType::RParen) {
        advance();
        return true;
    }
    bool sawKw = false;
    while (true) {
        // Distinguish keyword (Name '=' pattern) vs positional pattern.
        if (isName(cur_.type) && tok_.peek().type == TokenType::Assign) {
            std::string name = cur_.value;
            advance(); // name
            advance(); // =
            auto p = parsePattern();
            if (!p) return false;
            dst->kwargs.emplace_back(std::move(name), std::move(p));
            sawKw = true;
        } else {
            if (sawKw) {
                error("positional pattern follows keyword pattern");
                return false;
            }
            auto p = parsePattern();
            if (!p) return false;
            dst->args.push_back(std::move(p));
        }
        if (cur_.type == TokenType::Comma) {
            advance();
            if (cur_.type == TokenType::RParen) break;
            continue;
        }
        break;
    }
    if (!expect(TokenType::RParen)) return false;
    return true;
}

std::unique_ptr<MatchPatternNode> Parser::parsePattern() {
    auto first = parseClosedPattern();
    if (!first) return nullptr;
    // OR-pattern: a | b | c
    if (cur_.type == TokenType::BitOr) {
        auto orp = createNode<MatchOrPatternNode>();
        orp->alternatives.push_back(std::move(first));
        while (cur_.type == TokenType::BitOr) {
            advance();
            auto nxt = parseClosedPattern();
            if (!nxt) return nullptr;
            orp->alternatives.push_back(std::move(nxt));
        }
        first = std::move(orp);
    }
    // AS-pattern: <pat> as name
    if (cur_.type == TokenType::As) {
        advance();
        if (!isName(cur_.type)) {
            error("expected name after 'as' in pattern");
            return nullptr;
        }
        if (cur_.value == "_") {
            error("cannot use '_' as binding name in 'as' pattern");
            return nullptr;
        }
        auto asp = createNode<MatchAsPatternNode>();
        asp->pattern = std::move(first);
        asp->name = cur_.value;
        advance();
        return asp;
    }
    return first;
}

std::unique_ptr<ASTNode> Parser::parseMatch() {
    advance(); // match
    auto subject = parseTestList();
    if (!subject) return nullptr;
    if (!expect(TokenType::Colon)) return nullptr;
    skipNewlines();
    if (!expect(TokenType::Indent)) return nullptr;

    auto m = createNode<MatchNode>();
    m->subject = std::move(subject);

    while (cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile) {
        skipTrash();
        if (cur_.type == TokenType::Dedent || cur_.type == TokenType::EndOfFile) break;
        if (cur_.type != TokenType::Case) {
            error(std::string("expected 'case' in match block, got ") + tokenToName(cur_.type));
            return nullptr;
        }
        advance(); // case
        auto pat = parsePattern();
        if (!pat) return nullptr;
        std::unique_ptr<ASTNode> guard;
        if (cur_.type == TokenType::If) {
            advance();
            guard = parseExpression();
            if (!guard) return nullptr;
        }
        if (!expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;

        auto mc = createNode<MatchCaseNode>();
        mc->pattern = std::move(pat);
        mc->guard = std::move(guard);
        mc->body = std::move(body);
        m->cases.push_back(std::move(mc));
        skipNewlines();
    }
    expect(TokenType::Dedent);
    return m;
}

} // namespace protoPython
