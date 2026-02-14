#ifndef PROTOPYTHON_TOKENIZER_H
#define PROTOPYTHON_TOKENIZER_H

#include <string>
#include <cstddef>
#include <vector>

namespace protoPython {

enum class TokenType {
    Number,
    String,
    FString,
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
    Elif,
    Else,
    Global,
    Def,
    Pass,
    Indent,
    Dedent,
    Import,
    From,
    Class,
    Return,
    While,
    True,
    False,
    None,
    And,
    Or,
    Not,
    Try,
    Except,
    Finally,
    Raise,
    Break,
    Continue,
    Lambda,
    With,
    As,
    Is,
    IsNot,
    NotIn,
    Modulo,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Yield,
    Ellipsis,
    Error,
    Semicolon,
    PlusAssign,    /* += */
    MinusAssign,   /* -= */
    StarAssign,    /* *= */
    SlashAssign,   /* /= */
    Tilde,         /* ~ */
    Del,
    Assert,
    Nonlocal,
    At,            /* @ */
    DoubleStar,    /* ** */
    Async,
    Await,
    BitAnd,        /* & */
    BitOr,         /* | */
    BitXor,        /* ^ */
    LShift,        /* << */
    RShift,        /* >> */
    AndAssign,     /* &= */
    OrAssign,      /* |= */
    XorAssign,     /* ^= */
    LShiftAssign,  /* <<= */
    RShiftAssign,  /* >>= */
    Arrow,         /* -> */
    DoubleSlash,    /* // */
    DoubleSlashAssign, /* //= */
    Walrus,        /* := */
    AtAssign,      /* @= */
    ModuloAssign,  /* %= */
    DoubleStarAssign, /* **= */
    Type,          /* type (PEP 695) */
    Match,         /* match (soft keyword) */
    Case,          /* case (soft keyword) */
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string value;
    double numValue = 0.0;
    bool isInteger = false;
    long long intValue = 0;
    int line = 1;
    int column = 1;
};

/** Minimal Python tokenizer for expressions and simple statements. */
class Tokenizer {
public:
    explicit Tokenizer(const std::string& source);
    Token next();
    const Token& peek();
    bool hasNext() const;

    int getLine() const { return line_; }
    int getColumn() const { return pos_ - lineStartPos_ + 1; }

private:
    std::string source_;
    size_t pos_ = 0;
    int line_ = 1;
    size_t lineStartPos_ = 0;
    Token peeked_;
    bool hasPeeked_ = false;
    bool atLineStart_ = true;
    std::vector<int> indentStack_;
    int nestingLevel_ = 0;
    void skipWhitespace();
    void skipWhitespaceNoNewline();
    void skipComment();
    Token scanNumber();
    Token scanString(char quote, const std::string& prefix = "");
    Token scanNameOrKeyword();
    Token makeToken(TokenType type);
};

} // namespace protoPython

#endif
