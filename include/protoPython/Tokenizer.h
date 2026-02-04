#ifndef PROTOPYTHON_TOKENIZER_H
#define PROTOPYTHON_TOKENIZER_H

#include <string>
#include <cstddef>

namespace protoPython {

enum class TokenType {
    Number,
    String,
    Name,
    Plus,
    Minus,
    Star,
    Slash,
    LParen,
    RParen,
    Comma,
    Newline,
    EndOfFile,
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string value;
    double numValue = 0.0;
    bool isInteger = false;
    long long intValue = 0;
};

/** Minimal Python tokenizer for expressions and simple statements. */
class Tokenizer {
public:
    explicit Tokenizer(const std::string& source);
    Token next();
    const Token& peek();
    bool hasNext() const;

private:
    std::string source_;
    size_t pos_ = 0;
    Token peeked_;
    bool hasPeeked_ = false;
    void skipWhitespace();
    void skipComment();
    Token scanNumber();
    Token scanString(char quote);
    Token scanNameOrKeyword();
};

} // namespace protoPython

#endif
