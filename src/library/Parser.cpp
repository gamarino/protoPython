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

std::unique_ptr<ASTNode> Parser::parsePrimary() {
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
        expect(TokenType::RParen);
        return e;
    }
    return nullptr;
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

std::unique_ptr<ASTNode> Parser::parseExpression() {
    return parseOrExpr();
}

std::unique_ptr<ModuleNode> Parser::parseModule() {
    auto mod = std::make_unique<ModuleNode>();
    auto e = parseOrExpr();
    if (e)
        mod->body.push_back(std::move(e));
    return mod;
}

} // namespace protoPython
