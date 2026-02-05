#ifndef PROTOPYTHON_TOKENIZER_H
#define PROTOPYTHON_TOKENIZER_H

#include <string>
#include <cstddef>
#include <vector>

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
    // Step 5: attribute, subscript, literals, control flow
    Dot,
    LSquare,
    RSquare,
    LCurly,
    RCurly,
    Colon,
    Assign,
    EqEqual,  /* == */
    For,
    In,
    If,
    Else,
    Global,
    Def,
    Pass,
    Indent,
    Dedent,
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
    bool atLineStart_ = true;
    std::vector<int> indentStack_;
    void skipWhitespace();
    void skipWhitespaceNoNewline();
    void skipComment();
    Token scanNumber();
    Token scanString(char quote);
    Token scanNameOrKeyword();
};

} // namespace protoPython

#endif
