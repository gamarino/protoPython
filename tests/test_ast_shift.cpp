#include "protoPython/Parser.h"
#include "protoPython/Tokenizer.h"
#include <iostream>
#include <string>

using namespace protoPython;

// Helper to stringify tokens
std::string tokenName(TokenType t) {
    switch (t) {
        case TokenType::Name: return "Name";
        case TokenType::Number: return "Number";
        case TokenType::String: return "String";
        case TokenType::Assign: return "Assign";
        case TokenType::LShift: return "LShift";
        case TokenType::RShift: return "RShift";
        case TokenType::LParen: return "LParen";
        case TokenType::RParen: return "RParen";
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Newline: return "Newline";
        default: return "Token(" + std::to_string(static_cast<int>(t)) + ")";
    }
}

int main() {
    std::string source = "x = 1 >> 1\nprint(x)\n";
    Tokenizer tok(source);

    std::cout << "--- Tokens ---\n";
    while (true) {
        Token t = tok.next();
        std::cout << tokenName(t.type) << " '" << t.value << "' line=" << t.line << "\n";
        if (t.type == TokenType::EndOfFile) break;
    }

    std::cout << "\n--- AST ---\n";
    Parser parser(source);
    auto mod = parser.parseModule();
    if (parser.hasError()) {
        std::cout << "Parser error: " << parser.getLastErrorMsg() << " at line " << parser.getLastErrorLine() << "\n";
    }

    if (mod) {
        for (const auto& stmt : mod->body) {
            std::cout << "Stmt type: " << typeid(*stmt).name() << " line=" << stmt->line << "\n";
            if (auto b = dynamic_cast<AssignNode*>(stmt.get())) {
                std::cout << "  Assign right type: " << typeid(*(b->value)).name() << "\n";
                if (auto bop = dynamic_cast<BinOpNode*>(b->value.get())) {
                    std::cout << "    BinOp op=" << tokenName(bop->op) << " left_type=" << typeid(*(bop->left)).name() << "\n";
                }
            }
        }
    }
    return 0;
}
