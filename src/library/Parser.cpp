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
    
    while (accept(TokenType::Comma)) {
        if (cur_.type == TokenType::RSquare) break;
        auto p2 = parsePart();
        if (p2) parts.push_back(std::move(p2));
    }
    expect(TokenType::RSquare);
    
    if (parts.size() == 1) return std::move(parts[0]);
    
    auto t = createNode<TupleLiteralNode>();
    t->elements = std::move(parts);
    return t;
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
    if (cur_.type == TokenType::Await) {
        advance();
        auto a = createNode<AwaitNode>();
        a->value = parsePrimary(); 
        return a;
    }
    if (cur_.type == TokenType::String || cur_.type == TokenType::FString) {
        std::vector<std::unique_ptr<ASTNode>> allValues;
        bool anyF = false;
        
        while (cur_.type == TokenType::String || cur_.type == TokenType::FString) {
            if (cur_.type == TokenType::String) {
                auto n = createNode<ConstantNode>();
                n->constType = ConstantNode::ConstType::Str;
                n->strVal = cur_.value;
                allValues.push_back(std::move(n));
                advance();
            } else {
                anyF = true;
                auto fs = parseFString();
                if (!fs) return nullptr;
                if (auto* jsn = dynamic_cast<JoinedStrNode*>(fs.get())) {
                    for (auto& v : jsn->values) {
                        allValues.push_back(std::move(v));
                    }
                } else {
                    allValues.push_back(std::move(fs));
                }
            }
        }
        
        if (!anyF) {
            std::string finalVal;
            for (auto& v : allValues) {
                if (auto* cn = dynamic_cast<ConstantNode*>(v.get())) {
                    finalVal += cn->strVal;
                }
            }
            auto n = createNode<ConstantNode>();
            n->constType = ConstantNode::ConstType::Str;
            n->strVal = finalVal;
            return n;
        } else {
            auto res = createNode<JoinedStrNode>();
            res->values = std::move(allValues);
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
        skipTrash();
        if (accept(TokenType::RParen)) {
            return createNode<TupleLiteralNode>();
        }
        
        if (cur_.type == TokenType::Yield) {
            auto y = parseYieldExpression();
            if (!expect(TokenType::RParen)) return nullptr;
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

        if (!isStarred && cur_.type == TokenType::For) {
            auto ge = createNode<GeneratorExpNode>();
            ge->elt = std::move(e);
            ge->generators = parseComprehensions();
            expect(TokenType::RParen);
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

        if (!isStarred && cur_.type == TokenType::For) {
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

        if (!isStarred && !isDoubleStarred && cur_.type == TokenType::For) {
            auto sc = createNode<SetCompNode>();
            sc->elt = std::move(firstKey);
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
        
        if (isDoubleStarred || accept(TokenType::Colon)) {
            // Dict literal
            auto d = createNode<DictLiteralNode>();
            if (isDoubleStarred) {
                d->keys.push_back(nullptr);
                d->values.push_back(std::move(firstKey));
            } else {
                auto val = parseExpression();
                if (cur_.type == TokenType::For) {
                    auto dc = createNode<DictCompNode>();
                    dc->key = std::move(firstKey);
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
        n->constType = ConstantNode::ConstType::None;
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
            if (cur_.type != TokenType::Name && cur_.type != TokenType::Type && cur_.type != TokenType::Match && cur_.type != TokenType::Case) return left;
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
                    call->keywords.push_back({kwname, parseExpression()});
                } else {
                    auto arg = parseExpression();
                    if (call->args.empty() && call->keywords.empty() && cur_.type == TokenType::For) {
                        // list(x for x in y)
                        auto ge = createNode<GeneratorExpNode>();
                        ge->elt = std::move(arg);
                        ge->generators = parseComprehensions();
                        call->args.push_back(std::move(ge));
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
                    fn->parameter_annotations[varName] = parseExpression();
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
                fn->parameter_annotations[kwName] = parseExpression();
            }
        } else if (cur_.type == TokenType::Name || cur_.type == TokenType::Type || cur_.type == TokenType::Match || cur_.type == TokenType::Case) {
            std::string paramName = cur_.value;
            advance();
            if (accept(TokenType::Colon)) {
                fn->parameter_annotations[paramName] = parseExpression();
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
            a->value = parseExpression();
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
        bool comp = isCompound(cur_.type);
        auto st = parseStatement();
        if (st) mod->body.push_back(std::move(st));
        else if (!hasError_) {
            error(std::string("Unexpected token in module: ") + tokenToName(cur_.type));
            advance();
        }
        if (hasError_) break;
        if (!comp) {
            while (accept(TokenType::Semicolon) && cur_.type != TokenType::Newline && cur_.type != TokenType::EndOfFile) {
                auto s2 = parseStatement();
                if (s2) mod->body.push_back(std::move(s2));
                else break;
            }
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

    // Lambdas use parseParameters but without the name and often without parens in some grammars,
    // but in Python they are: lambda [parameters]: expression
    // Our parseParameters expects (.
    // Let's implement a simpler version for lambda or make parseParameters more flexible.
    if (cur_.type != TokenType::Colon) {
        // Simple parameter parsing for lambda
        bool isKwOnly = false;
        while (cur_.type == TokenType::Name || cur_.type == TokenType::Star || cur_.type == TokenType::DoubleStar) {
            if (cur_.type == TokenType::Star) {
                advance();
                if (cur_.type == TokenType::Name) {
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
        fn->returns = parseExpression();
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
        if (!expect(TokenType::Colon)) return nullptr;
        auto body = parseSuite();
        if (!body) return nullptr;
        
        auto fn = createNode<AsyncFunctionDefNode>();
        fn->name = funcName;
        fn->type_params = std::move(tmpFn->type_params);
        if (accept(TokenType::Arrow)) {
            fn->returns = parseExpression();
        }
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
    while (cur_.type == TokenType::For) {
        Comprehension c;
        advance(); // for
        c.target = parseTargetList();
        expect(TokenType::In);
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

std::unique_ptr<ASTNode> Parser::parseMatch() {
    advance(); // match
    auto subject = parseExpression();
    if (!expect(TokenType::Colon)) return nullptr;
    skipNewlines();
    if (!expect(TokenType::Indent)) return nullptr;
    while (cur_.type != TokenType::Dedent && cur_.type != TokenType::EndOfFile) {
        skipTrash();
        if (cur_.type == TokenType::Case) {
             advance(); // case
             // Match pattern can be complex, skip to colon
             while (cur_.type != TokenType::Colon && cur_.type != TokenType::EndOfFile) advance();
             if (!expect(TokenType::Colon)) return nullptr;
             auto s = parseSuite();
             if (!s) return nullptr;
        } else if (cur_.type == TokenType::Newline || cur_.type == TokenType::Indent) {
            advance();
        } else {
            error(std::string("expected 'case' in match block, got ") + tokenToName(cur_.type));
            break;
        }
        skipNewlines();
    }
    expect(TokenType::Dedent);
    return createNode<PassNode>(); 
}

} // namespace protoPython
